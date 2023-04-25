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
 */

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "maglev.hpp"

void print_buf(void* buf, const uint32_t nb_cache_lines) {
  for (uint32_t i = 0; i < nb_cache_lines * 64; i++) {
    printf("%02x ", ((uint8_t*)buf)[i]);
    if ((i + 1) % 8 == 0) {
      printf(" ");
    }
    if ((i + 1) % 16 == 0) {
      printf("\n");
    }
    if ((i + 1) % 64 == 0) {
      printf("\n");
    }
  }
}

void print_ips(uint8_t* pkt) {
  struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
  struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);

  std::cout << "src: " << ((l3->src_addr >> 0) & 0xff) << "."
            << ((l3->src_addr >> 8) & 0xff) << "."
            << ((l3->src_addr >> 16) & 0xff) << "."
            << ((l3->src_addr >> 24) & 0xff);

  std::cout << "  dst: " << ((l3->dst_addr >> 0) & 0xff) << "."
            << ((l3->dst_addr >> 8) & 0xff) << "."
            << ((l3->dst_addr >> 16) & 0xff) << "."
            << ((l3->dst_addr >> 24) & 0xff) << std::endl;
}

void init_pkt(uint8_t* pkt) {
  struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
  struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);
  struct rte_tcp_hdr* l4 = (struct rte_tcp_hdr*)(l3 + 1);

  l2->s_addr = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  l2->d_addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  l2->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  l3->version_ihl = RTE_IPV4_VHL_DEF;
  l3->type_of_service = 0;
  l3->total_length = rte_cpu_to_be_16(64 - 20 - 14 - 4);
  l3->packet_id = 0;
  l3->fragment_offset = 0;
  l3->time_to_live = 255;
  l3->next_proto_id = 6;  // TCP.
  l3->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 0, 0));
  l3->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));

  l4->src_port = 1234;
  l4->dst_port = 80;
}

void check_manually() {
  std::vector<uint32_t> backend_ips = {
      RTE_IPV4(10, 0, 0, 1), RTE_IPV4(10, 0, 0, 2), RTE_IPV4(10, 0, 0, 3),
      RTE_IPV4(10, 0, 0, 4)};

  uint8_t pkt[64];
  struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
  struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);

  init_pkt(pkt);

  Maglev maglev(backend_ips);

  int ret = maglev.setup();
  if (ret) {
    rte_exit(EXIT_FAILURE, "Issue setting up maglev : \"%s\"\n",
             rte_strerror(ret));
  }

  for (uint32_t i = 0; i < 4; i++) {
    l3->src_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 0, i));
    l3->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));

    std::cout << "Original packet:  ";
    print_ips(pkt);

    maglev.lookup(pkt);

    std::cout << "Modified packet:  ";
    print_ips(pkt);
  }
}

void check_distribution() {
  const uint32_t nb_ips = 32768;
  const uint32_t nb_backends = 1000;

  std::vector<uint32_t> backend_ips;

  for (uint32_t i = 0; i < nb_backends; ++i) {
    backend_ips.push_back(i);
  }

  uint8_t pkt[64];
  struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
  struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);

  init_pkt(pkt);

  Maglev maglev(backend_ips);

  int ret = maglev.setup();
  if (ret) {
    rte_exit(EXIT_FAILURE, "Issue setting up maglev : \"%s\"\n",
             rte_strerror(ret));
  }

  // Check uniformity.
  std::unordered_map<uint32_t, uint32_t> hist;
  for (uint32_t i = 0; i < nb_ips; i++) {
    l3->src_addr = i;
    l3->dst_addr = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 1));

    maglev.lookup(pkt);

    hist[l3->dst_addr]++;
  }

  assert(hist.size() == nb_backends);

  uint64_t sum = 0;
  for (auto& i : hist) {
    sum += i.second;
  }
  double mean = (double)sum / hist.size();
  double variance = 0;
  for (auto& i : hist) {
    variance += ((double)i.second - mean) * ((double)i.second - mean);
  }
  variance /= hist.size();
  double stddev = sqrt(variance);

  std::cout << std::endl
            << "Hits per backend IP: " << mean << "+-" << stddev << std::endl;
}

void check_time() {
  const uint64_t nb_trials = 1 << 25;
  const uint32_t nb_reps = 10;
  const uint32_t nb_backends = 1000;

  std::vector<uint32_t> backend_ips;

  for (uint32_t i = 0; i < nb_backends; ++i) {
    backend_ips.push_back(i);
  }

  uint32_t accum = 0;  // To prevent optimizations to remove lookup.

  std::vector<uint64_t> durations;

  for (uint32_t i = 0; i < nb_reps; ++i) {
    uint8_t pkt[64];
    struct rte_ether_hdr* l2 = (struct rte_ether_hdr*)pkt;
    struct rte_ipv4_hdr* l3 = (struct rte_ipv4_hdr*)(l2 + 1);

    init_pkt(pkt);

    Maglev maglev(backend_ips);

    int ret = maglev.setup();
    if (ret) {
      rte_exit(EXIT_FAILURE, "Issue setting up maglev : \"%s\"\n",
               rte_strerror(ret));
    }

    auto begin = std::chrono::steady_clock::now();
    for (uint64_t j = 0; j < nb_trials; ++j) {
      l3->dst_addr = (uint32_t)j;
      maglev.lookup(pkt);
      accum += l3->dst_addr;
    }
    auto end = std::chrono::steady_clock::now();

    auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count();

    durations.push_back(duration_ns / nb_trials);
  }

  uint64_t sum = 0;
  for (auto x : durations) {
    sum += x;
  }
  double mean = (double)sum / durations.size();
  double variance = 0;
  for (auto x : durations) {
    variance += (x - mean) * (x - mean);
  }
  variance /= durations.size();
  double stddev = sqrt(variance);

  std::cout << std::endl
            << "Lookup duration: " << mean << "+-" << stddev << "ns"
            << std::endl;
  std::cout << "accum: " << accum << std::endl;
}

int main(int argc, char** argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }

  argc -= ret;
  argv += ret;

  check_manually();
  check_distribution();
  check_time();

  return 0;
}
