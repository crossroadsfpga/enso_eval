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
#include <enso/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <sched.h>
#include <unistd.h>
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

#include "enso_userlib.h"

// From pcie.cpp. TODO(natre): De-duplicate.
static int get_hugepage_fd(const std::string path, size_t size) {
  int fd = open(path.c_str(), O_CREAT | O_RDWR, S_IRWXU);
  if (fd == -1) {
    std::cerr << "(" << errno << ") Problem opening huge page file descriptor"
              << std::endl;
    assert(false);
  }
  if (ftruncate(fd, (off_t)size)) {
    std::cerr << "(" << errno
              << ") Could not truncate huge page to size: " << size
              << std::endl;
    close(fd);
    unlink(path.c_str());
    assert(false);
  }
  return fd;
}

typedef struct {
  int alloc_idx;
  size_t length;
} tx_pending_request_t;

#define BUF_LEN 10000000

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

  uint32_t addr_offset = core_id * nb_queues;

  signal(SIGINT, int_handler);

  std::thread socket_thread = std::thread([&recv_bytes, addr_offset, nb_queues,
                                           &nb_batches, &nb_pkts] {
    uint32_t tx_pr_head = 0;
    uint32_t tx_pr_tail = 0;
    tx_pending_request_t* tx_pending_requests =
        new tx_pending_request_t[enso::kMaxPendingTxRequests + 1];

    std::this_thread::sleep_for(std::chrono::seconds(1));
    int cpu_id = sched_getcpu();  // Wait a bit, then get CPU id
    std::cout << "Running socket on CPU " << cpu_id << std::endl;

    for (int i = 0; i < nb_queues; ++i) {
      // TODO(sadok) can we make this a valid file descriptor?
      std::cout << "Creating queue " << i << std::endl;
      int socket_fd = enso::socket(AF_INET, SOCK_DGRAM, nb_queues);

      if (socket_fd == -1) {
        std::cerr << "Problem creating socket (" << errno
                  << "): " << strerror(errno) << std::endl;
        exit(2);
      }

      std::cout << "Done creating queue " << i << std::endl;
    }

    // TX managers
    uint8_t tx_alloc_rr_idx = 0;
    TXPacketQueueManager tx_manager{};
    const size_t txpd_size = kHugePageSize;  // TX ringbuffer size
    const std::string txpd_hp_prefix =
        ("/mnt/huge/set-intersect:" + std::to_string(sched_getcpu()) + "_txpd");

    for (uint8_t i = 0; i < TXPacketQueueManager::kNumAllocators; i++) {
      auto txpd_hp_path = txpd_hp_prefix + std::to_string(i);
      int txpd_fd = get_hugepage_fd(txpd_hp_path, txpd_size);
      tx_manager.initialize(i, txpd_fd, kHugePageSize);
    }

    setup_done = 1;
    unsigned char* buf;
    while (keep_running) {
      int socket_fd;
      int recv_len = enso::recv_select(0, &socket_fd, (void**)&buf, BUF_LEN, 0);

      if (unlikely(recv_len < 0)) {
        std::cerr << "Error receiving" << std::endl;
        exit(4);
      }

      if (likely(recv_len > 0)) {
        int processed_bytes = 0;
        uint8_t* pkt = buf;

        uint64_t phys_addr =
            tx_manager.get_allocator(tx_alloc_rr_idx).get_alloc_paddr();

        while (processed_bytes < recv_len) {
          uint16_t pkt_len = enso::get_pkt_len(pkt);
          uint16_t nb_flits = (pkt_len - 1) / 64 + 1;
          uint16_t pkt_aligned_len = nb_flits * 64;

          // Construct the response
          uint8_t* response =
              (uint8_t*)tx_manager.allocate(tx_alloc_rr_idx, pkt_aligned_len);
          assert(likely(response != NULL));  // Sanity check

          memcpy(response, pkt, pkt_aligned_len);
          struct rte_ether_hdr* l2_hdr = (struct rte_ether_hdr*)response;

          // Swap src/dst MAC fields
          auto original_src_mac = l2_hdr->s_addr;
          l2_hdr->s_addr = l2_hdr->d_addr;
          l2_hdr->d_addr = original_src_mac;

          pkt += pkt_aligned_len;  // Next packet
          processed_bytes += pkt_aligned_len;
          ++nb_pkts;
        }

        ++nb_batches;
        recv_bytes += recv_len;
        enso::free_enso_pipe(socket_fd, recv_len);
        enso::send(socket_fd, (void*)phys_addr, recv_len, 0);

        // TODO(sadok): This should be transparent to the app.
        // Save transmission request so that we can free the buffer once
        // it's complete.
        tx_pending_requests[tx_pr_tail].length = recv_len;
        tx_pending_requests[tx_pr_tail].alloc_idx = tx_alloc_rr_idx;
        tx_pr_tail = (tx_pr_tail + 1) % (enso::kMaxPendingTxRequests + 1);

        tx_alloc_rr_idx =
            ((tx_alloc_rr_idx + 1) % TXPacketQueueManager::kNumAllocators);
      }

      uint32_t nb_tx_completions = enso::get_completions(0);

      // Free data that was already sent.
      for (uint32_t i = 0; i < nb_tx_completions; ++i) {
        tx_manager.deallocate(tx_pending_requests[tx_pr_head].alloc_idx,
                              tx_pending_requests[tx_pr_head].length);
        tx_pr_head = (tx_pr_head + 1) % (enso::kMaxPendingTxRequests + 1);
      }
    }

    // TODO(sadok): it is also common to use the close() syscall to close a
    // UDP socket.
    for (int socket_fd = 0; socket_fd < nb_queues; ++socket_fd) {
      enso::print_sock_stats(socket_fd);
      enso::shutdown(socket_fd, SHUT_RDWR);
    }

    delete[] tx_pending_requests;
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

    std::this_thread::sleep_for(std::chrono::seconds(1));

    uint64_t delta_bytes = recv_bytes - recv_bytes_before;
    uint64_t delta_batches = nb_batches - nb_batches_before;
    std::cout << std::dec << delta_bytes * 8. / 1e6 << " Mbps  " << recv_bytes
              << " bytes  " << nb_batches << " batches  " << nb_pkts
              << " packets";

    if (delta_batches > 0) {
      std::cout << "  " << delta_bytes / delta_batches << " bytes/batch";
    }
    std::cout << std::endl;
  }

  std::cout << "Waiting for threads" << std::endl;

  socket_thread.join();

  return 0;
}
