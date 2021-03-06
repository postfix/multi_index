#pragma once

#include <iostream>
#include <algorithm>
#include <vector>
#include <limits>
#include <bitset>
#include "multi_idx/perm.hpp"
#include "sdsl/io.hpp"
#include "sdsl/int_vector.hpp"
#include "sdsl/sd_vector.hpp"
#include "sdsl/bit_vectors.hpp"
#include "multi_idx/multi_idx_helper.hpp"

namespace multi_index {
  
  template<uint8_t t_b=4,
           uint8_t t_k=3,
           size_t t_id=0,
           typename perm_b_k=perm<t_b,t_b-t_k>,
           uint8_t xor_len=8,
           typename t_bv=sdsl::bit_vector,
           typename t_sel=typename t_bv::select_1_type> 
  class _xor_buckets_binvector_split {
    public:
        typedef uint64_t size_type;
        typedef uint64_t entry_type;
        typedef perm_b_k perm;
        enum {id = t_id};

    private:  
        static constexpr uint8_t init_splitter_bits(size_t i=0){
            return i < perm_b_k::match_len ? perm_b_k::mi_permute_block_widths[t_id][t_b-1-i] + init_splitter_bits(i+1) : 0;
        }

        /* Low_* stuff control how many of the less signifigant bits form the lower part */
        static constexpr uint8_t    low_bits    = 32; // PLS, keep this a power of 2, better if word aligned
        static constexpr uint64_t   low_mask    = (1ULL<<low_bits)-1;
        static constexpr uint8_t    splitter_bits = init_splitter_bits();
        static constexpr uint8_t    mid_bits = 64 - (low_bits + splitter_bits);
        static constexpr uint8_t    mid_shift   = low_bits; 
        static constexpr uint64_t   mid_mask = (1ULL<<mid_bits)-1;
        static constexpr uint8_t    high_shift = (64-splitter_bits);
        using  mid_entries_type = typename mid_entries_trait<mid_bits>::type;
        
        sdsl::int_vector<64>        m_first_level;
        sdsl::int_vector<low_bits>  m_low_entries;
        mid_entries_type            m_mid_entries; 
        uint64_t                    m_n;      // number of items
        //sdsl::int_vector<64>  fake_entries; for DEBUG USE ONLY
        t_bv                        m_C;     // bit vector for prefix sums of meta-symbols
        t_sel                       m_C_sel; // select1 structure for m_C

    public:
        _xor_buckets_binvector_split() = default;

        _xor_buckets_binvector_split(const std::vector<entry_type> &input_entries) {
            std::cout << "Splitter bits " << (uint16_t) splitter_bits << std::endl; 
            m_n = input_entries.size();
            m_low_entries = sdsl::int_vector<low_bits>(input_entries.size(), 0); 
            m_mid_entries = mid_entries_trait<mid_bits>::get_instance(input_entries.size(), 0);
            
            build_small_universe(input_entries);
        }

        inline std::pair<std::vector<uint64_t>, uint64_t> match(const entry_type q, uint8_t errors=t_k, const bool find_only_candidates=false) const {
            const uint64_t bucket = get_bucket_id(q);
            
            const auto l = bucket == 0 ? 0 : m_C_sel(bucket) - bucket +1; 
            const auto r = m_C_sel(bucket+1) - (bucket+1) + 1;  
    
            uint64_t candidates = r-l;
            std::vector<entry_type> res;
            
            if(find_only_candidates) return {res, candidates};
  
            const uint64_t q_permuted   = perm_b_k::mi_permute[t_id](q); 
            const uint64_t q_high       = (q_permuted>>(high_shift))<<high_shift;
            const uint64_t q_low        = q_permuted & low_mask;
            const uint64_t q_xor        = get_xor(q);
            const uint64_t mask         = (1<<xor_len) - 1;
            
            const auto fl_begin  = m_first_level.begin() + l;
            const auto fl_end    = m_first_level.begin() + r; 
            for(auto fl_it = fl_begin; fl_it != fl_end; ++fl_it)  {
              const uint64_t el_xor = *fl_it & mask; 
              if(sdsl::bits::cnt(q_xor^el_xor) <= errors) {
              
                uint64_t pos_l = *fl_it >> xor_len;
                const uint64_t pos_r = *(fl_it+1) >> xor_len; 
              
                const auto begin  = m_low_entries.begin() + pos_l;
                const auto end    = m_low_entries.begin() + pos_r;         
                for (auto it = begin; it != end; ++it, ++pos_l) {
                  const uint64_t item_low = *it;  
                  if(sdsl::bits::cnt(q_low^item_low) <= errors) {
                    const uint64_t curr_el = q_high | (((uint64_t) m_mid_entries[pos_l]) << mid_shift) | item_low;
                    if(sdsl::bits::cnt(q_permuted^curr_el) <= errors) {
                       res.push_back(perm_b_k::mi_rev_permute[t_id](curr_el));
                    }
                  }
                }
              }
            }
            return {res, candidates};
        }
  
        _xor_buckets_binvector_split& operator=(const _xor_buckets_binvector_split& idx) {
            if ( this != &idx ) {
                m_n       = std::move(idx.m_n);
                m_low_entries   = std::move(idx.m_low_entries);
                m_mid_entries   = std::move(idx.m_mid_entries);
                m_first_level   = std::move(idx.m_first_level);
                //fake_entries   = std::move(idx.fake_entries);           
                m_C           = std::move(idx.m_C);
                m_C_sel       = std::move(idx.m_C_sel);
                m_C_sel.set_vector(&m_C);
            }
            return *this;
        }

        _xor_buckets_binvector_split& operator=(_xor_buckets_binvector_split&& idx) {
            if ( this != &idx ) {
                m_n       = std::move(idx.m_n);
                m_low_entries   = std::move(idx.m_low_entries);
                m_mid_entries   = std::move(idx.m_mid_entries);
                m_first_level   = std::move(idx.m_first_level);
                //fake_entries   = std::move(idx.fake_entries);                               
                m_C           = std::move(idx.m_C);
                m_C_sel       = std::move(idx.m_C_sel);
                m_C_sel.set_vector(&m_C);
            }
            return *this;
        }

        _xor_buckets_binvector_split(const _xor_buckets_binvector_split& idx) {
            *this = idx;
        }

        _xor_buckets_binvector_split(_xor_buckets_binvector_split&& idx){
            *this = std::move(idx);
        }

        //! Serializes the data structure into the given ostream
        size_type serialize(std::ostream& out, sdsl::structure_tree_node* v=nullptr, std::string name="")const {
            using namespace sdsl;
            structure_tree_node* child = structure_tree::add_child(v, name, util::class_name(*this));
            uint64_t written_bytes = 0;
            written_bytes += write_member(m_n, out, child, "n");
            written_bytes += m_low_entries.serialize(out, child, "low_entries"); 
            written_bytes += m_mid_entries.serialize(out, child, "mid_entries"); 
            written_bytes += m_first_level.serialize(out, child, "first_level"); 
            written_bytes += m_C.serialize(out, child, "C");
            written_bytes += m_C_sel.serialize(out, child, "C_sel");  
            //written_bytes += fake_entries.serialize(out, child, "fake");  
            structure_tree::add_size(child, written_bytes);
            return written_bytes;
        }

        //! Loads the data structure from the given istream.
        void load(std::istream& in) {
            using namespace sdsl;
            read_member(m_n, in);
            m_low_entries.load(in);
            m_mid_entries.load(in);
            m_first_level.load(in);
            m_C.load(in);
            m_C_sel.load(in, &m_C);
            //fake_entries.load(in);
        }

        size_type size() const{
            return m_n;
        }

private:

    inline uint64_t get_bucket_id(const uint64_t x) const {
        return perm_b_k::mi_permute[t_id](x) >> (64-splitter_bits);
    }
    
    inline uint64_t get_bucket_xor_id(const uint64_t x) const {
        return (get_bucket_id(x)<<xor_len) | get_xor(x);
    }
    
    inline uint64_t get_xor(const uint64_t x) const {
      uint64_t res = 0, mask = (1<<xor_len) - 1;
      for(size_t i = 0; i < 64; i+=xor_len) {
        uint64_t l = (x >> i) & mask;
        res = res ^ l;
      }
      return res;
    }

    void build_small_universe(const std::vector<entry_type> &input_entries) {
        // Implement a countingSort-like strategy to order entries accordingly to
        // their splitter_bits MOST significant bits
        // Ranges of keys having the same MSB are not sorted. 
        uint64_t splitter_universe = ((uint64_t) 1) << (splitter_bits+xor_len);

        std::vector<uint64_t> prefix_sums(splitter_universe + 1, 0); // includes a sentinel
        for (auto x: input_entries) {
            prefix_sums[get_bucket_xor_id(x)]++;
        }
        
        // m_C = t_bv(splitter_universe+input_entries.size(), 0);
        // size_t idx = 0;
        // for(auto x : prefix_sums) {
        //   for(size_t i = 0; i < x; ++i, ++idx)
        //     m_C[idx] = 0;
        //   m_C[idx++] = 1;
        // }
        // m_C_sel = t_sel(&m_C);
        
        uint64_t sum = prefix_sums[0];
        prefix_sums[0] = 0;
        for(uint64_t i = 1; i < prefix_sums.size(); ++i) {
            uint64_t curr = prefix_sums[i];
            prefix_sums[i] = sum + i; // +i is to obtain a striclty monotone sequence as we would have with binary vectors
            sum += curr;
        }

        std::vector<uint32_t> bucket_xor_ids(input_entries.size(), 0);
        // Partition elements into buckets accordingly to their less significant bits
        for (auto &x : input_entries) {
            uint64_t bucket = get_bucket_xor_id(x);
            uint64_t permuted_item = perm_b_k::mi_permute[t_id](x);
            //fake_entries[prefix_sums[bucket]-bucket]   = permuted_item;
            m_mid_entries[prefix_sums[bucket]-bucket]  = (permuted_item>>mid_shift) & mid_mask; // -bucket is because we have a striclty monotone sequence 
            m_low_entries[prefix_sums[bucket]-bucket] = (permuted_item & low_mask);
            bucket_xor_ids[prefix_sums[bucket]-bucket] = bucket;
            prefix_sums[bucket]++;
        }
        
        const uint64_t mask = (1<<xor_len) - 1;
        uint64_t prev_bucket = bucket_xor_ids[0] >> xor_len;
        size_t binvector_size = 1ULL << splitter_bits;
        uint64_t prev_xor = bucket_xor_ids[0] & mask;
        uint64_t pos = 0;
        
        std::vector<uint64_t> fl;
        std::vector<uint8_t> bv;
        
        for(size_t j = 0; j < prev_bucket; j++)
          bv.push_back(1);
        bv.push_back(0);
        fl.push_back((pos << xor_len) | prev_xor);

        for(auto &x: bucket_xor_ids) {
          const uint64_t curr_bucket = x >> xor_len;
          const uint64_t curr_xor = x & mask;
          if(curr_bucket != prev_bucket) {
            for(size_t j = prev_bucket; j < curr_bucket; j++) {
              bv.push_back(1);
            }
            bv.push_back(0);
            prev_bucket = curr_bucket;
            fl.push_back((pos << xor_len) | curr_xor);
            prev_xor = curr_xor;
            binvector_size++;
            pos++;
            continue;
          }
          if(prev_xor > curr_xor) std::cout << "ERROR " << std::endl;
          if(prev_xor != curr_xor) {
            fl.push_back((pos << xor_len) | curr_xor);
            bv.push_back(0);
            prev_xor = curr_xor;
            binvector_size++;
          }
          pos++;
        } 
        bv.push_back(1);
        fl.push_back((pos << xor_len) | 0); // sentinel. We will access only pos on extreme cases.
        
        std::cout << "FL " << fl.size() << " BV " << bv.size() << std::endl;
        
        
        m_first_level = sdsl::int_vector<64>(fl.size(),0);
        for(size_t i = 0; i < fl.size(); ++i)
          m_first_level[i] = fl[i];
        m_C = t_bv(bv.size(), 0);
        
        for(size_t i = 0; i < bv.size(); ++i)
          m_C[i] = bv[i];
        m_C_sel = t_sel(&m_C);
        
    }
};

template<uint8_t xor_len=6,
         typename t_bv=sdsl::bit_vector,
         typename t_sel=typename t_bv::select_1_type
       >
struct xor_buckets_binvector_split {
  template<uint8_t t_b, uint8_t t_k, size_t t_id, typename t_perm>
  using type = _xor_buckets_binvector_split<t_b, t_k, t_id, t_perm, xor_len, t_bv, t_sel>;
};

}
