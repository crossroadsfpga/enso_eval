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

#include <enso/consts.h>
#include <enso/helpers.h>
#include <enso/pipe.h>
#include <pthread.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <sched.h>
#include <x86intrin.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "constants.h"  // Include first
#include "geometric.h"
#include "minheap.h"
#include "nitrosketch.h"
#include "xxhash.h"

static volatile int keep_running = 1;
static volatile int setup_done = 0;

void int_handler(int signal __attribute__((unused))) { keep_running = 0; }

int main(int argc, char** argv) {
  int result;
  uint64_t recv_bytes = 0;
  uint64_t nb_batches = 0;
  uint64_t nb_pkts = 0;

  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " core nb_queues" << std::endl;
    return 1;
  }

  int core_id = atoi(argv[1]);
  int nb_queues = atoi(argv[2]);

  signal(SIGINT, int_handler);

  std::thread socket_thread =
      std::thread([&recv_bytes, nb_queues, &nb_batches, &nb_pkts] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Running socket on CPU " << sched_getcpu() << std::endl;

        auto device = enso::Device::Create(nb_queues);
        for (int idx = 0; idx < nb_queues; idx++) {
          device->AllocateRxTxPipe();
        }

        // NitroSketch: DS init
        uint64_t pkt_count = 0;
#ifdef NITRO_CMS
        CountMinSketch cm;
#endif

#ifdef NITRO_CS
        CountSketch cs;
#endif

    // NitroSketch: hook init
#ifdef NITRO_CMS
        cm_init(&cm, CM_COL_NO, 0.01);
#endif
#ifdef NITRO_CS
        cs_init(&cs, CS_COL_NO, 0.01);
#endif

        setup_done = 1;

        while (likely(keep_running)) {
          enso::RxTxPipe* rx_tx_pipe = device->NextRxTxPipeToRecv();
          if (rx_tx_pipe == nullptr) {
            continue;
          }

          auto batch = rx_tx_pipe->PeekPkts();
          for (auto pkt : batch) {
            pkt_count++;

            struct rte_ether_hdr* eth_hdr = ((struct rte_ether_hdr*)pkt);

            if (likely(eth_hdr->ether_type ==
                       rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))) {
              while (pkt_count >= cm.nextUpdate) {
                struct rte_ipv4_hdr* ip_hdr =
                    ((struct rte_ipv4_hdr*)(eth_hdr + 1));

                uint64_t flow_key =
                    (ip_hdr->src_addr | (((uint64_t)ip_hdr->dst_addr) << 32));

#ifdef NITRO_CMS
                cm_processing(&cm, flow_key);
#endif

#ifdef NITRO_CS
                cs_processing(&cs, flow_key);
#endif
              }
            }
            // Swap MAC
            auto temp_mac_addr = eth_hdr->s_addr;
            eth_hdr->s_addr = eth_hdr->d_addr;
            eth_hdr->d_addr = temp_mac_addr;

            ++nb_pkts;
          }
          uint32_t batch_length = batch.processed_bytes();
          rx_tx_pipe->ConfirmBytes(batch_length);

          ++nb_batches;
          recv_bytes += batch_length;
          rx_tx_pipe->SendAndFree(batch_length);
        }
        print_sketch(&cm, "output_enso_nitrosketch.txt");
      });

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  result = pthread_setaffinity_np(socket_thread.native_handle(), sizeof(cpuset),
                                  &cpuset);
  if (result < 0) {
    std::cerr << "Error setting CPU affinity" << std::endl;
    return 6;
  }

  while (!setup_done) continue;

  std::cout << "Starting..." << std::endl;

  while (keep_running) {
    uint64_t recv_bytes_before = recv_bytes;
    uint64_t nb_batches_before = nb_batches;
    uint64_t nb_pkts_before = nb_pkts;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    uint64_t delta_pkts = nb_pkts - nb_pkts_before;
    uint64_t delta_bytes = recv_bytes - recv_bytes_before;
    uint64_t delta_batches = nb_batches - nb_batches_before;
    std::cout << std::dec << (delta_bytes + delta_pkts * 20) * 8. / 1e6
              << " Mbps  " << recv_bytes << " bytes  " << nb_batches
              << " batches  " << nb_pkts << " packets";

    if (delta_batches > 0) {
      std::cout << "  " << delta_bytes / delta_batches << " bytes/batch";
    }
    std::cout << std::endl;
  }

  std::cout << "Waiting for threads" << std::endl;

  socket_thread.join();

  return 0;
}
