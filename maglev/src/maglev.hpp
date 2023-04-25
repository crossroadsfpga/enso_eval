/*
 * Copyright (c) 2022, Carnegie Mellon University
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *      * Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Implementation of Google's Maglev Load Balancer.
 * Eisenbud et al. (NSDI '16).
 *
 */

#ifndef MAGLEV_SRC_MAGLEV_HPP_
#define MAGLEV_SRC_MAGLEV_HPP_

#include <rte_common.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_fbk_hash.h>
#include <rte_ip.h>
#include <rte_jhash.h>
#include <rte_vect.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <unordered_map>
#include <vector>

/**
 * @brief Maglev load balancer.
 *
 * Must call DPDK function `rte_eal_init` before instantiating this class and
 * the `setup()` method before using it.
 */
class Maglev {
 public:
  explicit Maglev(const std::vector<uint32_t>& backend_ips)
      : nb_backends_(backend_ips.size()), permutations_(nb_backends_) {
    backend_ips_ = new uint32_t[nb_backends_];

    // Store all backend IPs in big endian.
    for (uint32_t i = 0; i < nb_backends_; ++i) {
      backend_ips_[i] = rte_cpu_to_be_32(backend_ips[i]);
    }

    for (uint32_t i = 0; i < kSize; ++i) {
      hash_table_[i] = 0xffff;
    }
  }

  Maglev(Maglev&&) = default;
  Maglev& operator=(Maglev&&) = default;

  Maglev(const Maglev&) = delete;
  Maglev& operator=(const Maglev&) = delete;

  ~Maglev() {
    rte_fbk_hash_free(ht_);
    delete[] backend_ips_;
  }

  int setup() {
    struct rte_fbk_hash_params hash_params;
    char hash_name[50];

    int lcore_id = sched_getcpu();
    if (lcore_id < 0) {
      return errno;
    }

    snprintf(hash_name, sizeof(hash_name), "hash_cache%03u", lcore_id);

    hash_params.name = hash_name;
    hash_params.entries = 1024;
    hash_params.entries_per_bucket = kEntriesPerBucket;
    hash_params.socket_id = rte_socket_id();
    hash_params.hash_func = NULL;
    hash_params.init_val = 0;

    ht_ = rte_fbk_hash_create(&hash_params);

    if (ht_ == NULL) {
      return rte_errno;
    }

    generate_permutations();
    populate();

#ifndef DISABLE_ASSERT
    // Sanity check.
    for (uint32_t i = 0; i < kSize; ++i) {
      assert(hash_table_[i] != 0xffff);
    }
#endif

    return 0;
  }

  __rte_always_inline void lookup(uint8_t* pkt) {
    struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
    struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);

    // XOR dst address and protocol in place to avoid having to copy 16 bytes.
    l3->dst_addr ^= (uint32_t)l3->next_proto_id;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    const uint32_t hash = rte_jhash_32b((uint32_t*)&(l3->src_addr), 3, 0);
#pragma GCC diagnostic pop

    // Restore dst address.
    l3->dst_addr ^= (uint32_t)l3->next_proto_id;

    uint16_t backend_id = get_cached_hash_value(ht_, hash);

    l3->dst_addr = backend_ips_[backend_id];
  }

  // Code adapted from DPDK 20.11 rte_fbk_hash_add_key_with_bucket so that if
  // there is no space left in the bucket we remove the oldest key.
  __rte_always_inline uint16_t get_cached_hash_value_with_bucket(
      struct rte_fbk_hash_table* ht, uint32_t hash, uint32_t bucket) {
    for (uint16_t i = 0; i < kEntriesPerBucket; i++) {
      uint16_t is_entry = ht->t[bucket + i].entry.is_entry;
      // Set entry if unused.
      if (!is_entry) {
        union rte_fbk_hash_entry new_entry;
        new_entry.whole_entry = ((uint64_t)(hash) << 32) |
                                ((uint64_t)(hash_table_[hash % kSize]) << 16) |
                                (uint64_t)1;
        ht->t[bucket + i].whole_entry = new_entry.whole_entry;

        // Keep track of the newest entry in the first element's `is_entry`.
        ht->t[bucket].entry.is_entry = (i << 1) | 1;

        ht->used_entries++;
        return new_entry.entry.value;
      }
      // Return value if hash already exists.
      if (ht->t[bucket + i].entry.key == hash) {
        return ht->t[bucket + i].entry.value;
      }
    }

    // Replace oldest entry.
    union rte_fbk_hash_entry new_entry;
    new_entry.whole_entry = ((uint64_t)(hash) << 32) |
                            ((uint64_t)(hash_table_[hash % kSize]) << 16) |
                            (uint64_t)1;

    uint32_t entry_id =
        ((ht->t[bucket].entry.is_entry >> 1) + 1) & (kEntriesPerBucket - 1);
    ht->t[bucket + entry_id].whole_entry = new_entry.whole_entry;

    // Keep track of the newest entry in the first element's `is_entry`.
    ht->t[bucket].entry.is_entry = ((uint16_t)entry_id << 1) | 1;
    return new_entry.entry.value;
  }

  __rte_always_inline uint16_t
  get_cached_hash_value(struct rte_fbk_hash_table* ht, uint32_t hash) {
    return get_cached_hash_value_with_bucket(ht, hash,
                                             rte_fbk_hash_get_bucket(ht, hash));
  }

 private:
  void generate_permutations() {
    std::vector<uint32_t> offsets(nb_backends_, 0);
    std::vector<uint32_t> skips(nb_backends_, 0);

    for (uint32_t i = 0; i < nb_backends_; ++i) {
      uint32_t hash1, hash2;
      hash1 = 0;
      hash2 = 1;
      rte_jhash_32b_2hashes(&backend_ips_[i], 1, &hash1, &hash2);

      offsets[i] = hash1 % kSize;
      skips[i] = (hash2 % (kSize - 1)) + 1;
    }

    for (uint32_t i = 0; i < nb_backends_; ++i) {
      for (uint32_t j = 0; j < kSize; ++j) {
        permutations_[i][j] = (offsets[i] + j * skips[i]) % kSize;
      }
    }
  }

  void populate() {
    std::vector<uint32_t> next(nb_backends_, 0);
    uint32_t n = 0;

    while (true) {
      for (uint32_t i = 0; i < nb_backends_; ++i) {
        uint32_t c = permutations_[i][next[i]];

        while (hash_table_[c] != 0xffff) {
          ++(next[i]);
          c = permutations_[i][next[i]];
        }
        hash_table_[c] = i;

        ++(next[i]);
        ++n;

        if (n == kSize) {
          return;
        }
      }
    }
  }

  static constexpr uint32_t kSize = 65537;  // Must be prime.
  static constexpr uint32_t kEntriesPerBucket = 4;
  const uint32_t nb_backends_;
  uint32_t* backend_ips_;
  std::array<uint16_t, kSize> hash_table_;
  std::vector<std::array<uint32_t, kSize>> permutations_;
  struct rte_fbk_hash_table* ht_;
};

#endif  // MAGLEV_SRC_MAGLEV_HPP_
