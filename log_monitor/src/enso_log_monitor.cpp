/*
 * Copyright (c) 2023, Carnegie Mellon University
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

#include <arpa/inet.h>
#include <enso/consts.h>
#include <enso/helpers.h>
#include <enso/pipe.h>
#include <rte_errno.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include "log_monitor.hpp"

static volatile bool keep_running = true;
static volatile bool setup_done = false;

const uint32_t kMaxBatchSize = enso::kBufPageSize;
const uint32_t kBaseIpAddress = ntohl(inet_addr("192.168.0.0"));
const uint32_t kDstPort = 80;
const uint32_t kProtocol = 0x11;

void int_handler([[maybe_unused]] int signal) { keep_running = false; }

void run_echo_event(uint32_t nb_streams, uint32_t core_id,
                    const std::string& regex_filename, enso::stats_t* stats) {
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "Running on core " << sched_getcpu() << std::endl;

  using enso::Device;
  using enso::RxPipe;

  std::unique_ptr<Device> dev = Device::Create(nb_streams, core_id);
  std::vector<RxPipe*> pipes;

  if (!dev) {
    std::cerr << "Problem creating device" << std::endl;
    exit(2);
  }

  for (uint64_t stream_id = 0; stream_id < nb_streams; ++stream_id) {
    RxPipe* pipe = dev->AllocateRxPipe();
    if (!pipe) {
      std::cerr << "Problem creating RX pipe" << std::endl;
      exit(3);
    }
    uint32_t dst_ip = kBaseIpAddress + core_id * nb_streams + stream_id;
    pipe->Bind(kDstPort, 0, dst_ip, 0, kProtocol);
    pipe->set_context((void*)stream_id);

    pipes.push_back(pipe);
  }

  LogMonitor log_monitor(regex_filename, nb_streams);
  int ret = log_monitor.setup();
  if (ret) {
    std::cerr << "Issue setting up log monitor: \"" << rte_strerror(ret) << "\""
              << std::endl;
    exit(4);
  }

  setup_done = true;

  uint64_t nb_matches = 0;

  while (keep_running) {
    RxPipe* pipe = dev->NextRxPipeToRecv();

    if (unlikely(pipe == nullptr)) {
      continue;
    }

    uint32_t stream_id = (uint64_t)pipe->context();
    uint8_t* buf;
    uint32_t recv_len = pipe->Recv(&buf, kMaxBatchSize);

    nb_matches += log_monitor.lookup(buf, recv_len, stream_id);

    pipe->Free(recv_len);

    stats->recv_bytes += recv_len;
    ++(stats->nb_batches);
  }

  std::cout << "Total matches: " << nb_matches << std::endl;
}

int main(int argc, const char* argv[]) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0]
              << " NB_CORES NB_STREAMS REGEX_FILENAME NB_STREAMS" << std::endl
              << std::endl;
    std::cerr << "NB_CORES: Number of cores to use." << std::endl;
    std::cerr << "NB_STREAMS: Number of streams to monitor per core."
              << std::endl;
    std::cerr << "REGEX_FILENAME: File with regular expressions." << std::endl;
    return 1;
  }

  uint32_t nb_cores = atoi(argv[1]);
  uint32_t nb_streams = atoi(argv[2]);
  std::string regex_filename = argv[3];

  signal(SIGINT, int_handler);

  std::vector<std::thread> threads;
  std::vector<enso::stats_t> thread_stats(nb_cores);

  for (uint32_t core_id = 0; core_id < nb_cores; ++core_id) {
    threads.emplace_back(run_echo_event, nb_streams, core_id, regex_filename,
                         &(thread_stats[core_id]));
    if (enso::set_core_id(threads.back(), core_id)) {
      std::cerr << "Error setting CPU affinity" << std::endl;
      return 6;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  while (!setup_done) continue;  // Wait for setup to be done.

  show_stats(thread_stats, &keep_running);

  for (auto& thread : threads) {
    thread.join();
  }

  return 0;
}
