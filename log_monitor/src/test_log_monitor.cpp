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

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "log_monitor.hpp"

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

void check_time(const std::string regex_filename,
                const std::string log_filename) {
  const uint64_t nb_trials = 1;
  const uint32_t nb_reps = 10;
  const uint32_t nb_streams = 1024;

  std::vector<uint64_t> durations;

  uint64_t nb_matches = 0;

  std::array<uint8_t*, nb_streams> buffers;
  uint64_t buffer_size;
  for (uint32_t i = 0; i < nb_streams; i++) {
    std::ifstream log_file(log_filename);
    log_file.seekg(0, std::ios::end);
    buffer_size = log_file.tellg();
    log_file.seekg(0, std::ios::beg);

    buffers[i] = new uint8_t[buffer_size];
    log_file.read(reinterpret_cast<char*>(buffers[i]), buffer_size);
  }

  for (uint32_t i = 0; i < nb_reps; ++i) {
    LogMonitor log_monitor(regex_filename, nb_streams);

    int ret = log_monitor.setup();
    if (ret) {
      rte_exit(EXIT_FAILURE, "Issue setting up log monitor\n");
    }

    auto begin = std::chrono::steady_clock::now();
    for (uint64_t j = 0; j < nb_trials; ++j) {
      for (uint64_t k = 0; k < nb_streams; ++k) {
        ret = log_monitor.lookup(buffers[k], buffer_size, k);
        if (unlikely(ret < 0)) {
          rte_exit(EXIT_FAILURE, "Issue looking up log monitor\n");
        }
        nb_matches += ret;
      }
    }
    auto end = std::chrono::steady_clock::now();

    auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count();

    durations.push_back(duration_ns / nb_trials / nb_streams /
                        (buffer_size / 1460));
    // durations.push_back(duration_ns);
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

  std::cout << "nb_matches: " << nb_matches << std::endl;
  std::cout << std::endl
            << "Lookup duration: " << mean << "+-" << stddev << "ns/MSS"
            << std::endl;

  for (uint32_t i = 0; i < nb_streams; i++) {
    delete[] buffers[i];
  }

  std::ofstream out_file("out.txt", std::ios::out | std::ios::app);
  out_file << regex_filename << ": " << mean << "+-" << stddev << "ns/MSS"
           << std::endl;
}

int main(int argc, char** argv) {
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }

  argc -= ret;
  argv += ret;

  if (argc != 3) {
    rte_exit(EXIT_FAILURE, "Usage: %s <regex_filename> <log_filename>\n",
             argv[0]);
  }

  const std::string kRegexFilename = argv[1];
  const std::string kLogFilename = argv[2];

  // check_manually();
  // check_distribution();
  check_time(kRegexFilename, kLogFilename);

  return 0;
}
