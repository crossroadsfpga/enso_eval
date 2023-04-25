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

#include "enso_userlib.h"

#include <assert.h>
#include <enso/helpers.h>
#include <enso/socket.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>

// From pcie.cpp. TODO(natre): De-duplicate.
static uint64_t virt_to_phys(void* virt) {
  long pagesize = sysconf(_SC_PAGESIZE);
  int fd = open("/proc/self/pagemap", O_RDONLY);
  if (fd < 0) {
    return 0;
  }
  // pagemap is an array of pointers for each normal-sized page
  if (lseek(fd, (uintptr_t)virt / pagesize * sizeof(uintptr_t), SEEK_SET) < 0) {
    close(fd);
    return 0;
  }

  uintptr_t phy = 0;
  if (read(fd, &phy, sizeof(phy)) < 0) {
    close(fd);
    return 0;
  }
  close(fd);

  if (!phy) {
    return 0;
  }
  // bits 0-54 are the page number
  return (uint64_t)((phy & 0x7fffffffffffffULL) * pagesize +
                    ((uintptr_t)virt) % pagesize);
}

/**
 * SpeculativeRingBufferMemoryAllocator implementation.
 */
void RingBufferMemoryAllocator::initialize(const int buffer_fd,
                                           const uint64_t buffer_size) {
  assert(buffer_vaddr_ == nullptr);  // Sanity check

  // Initialize the virtual addrspace
  buffer_vaddr_ = (void*)mmap(NULL, buffer_size * 2, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_HUGETLB, buffer_fd, 0);

  if (buffer_vaddr_ == (void*)-1) {
    std::cerr << "(" << errno << ") Could not mmap huge page" << std::endl;
    close(buffer_fd);
    assert(false);
  }

  // Re-map the second half of the virtual addrspace onto the buffer
  void* ret = (void*)mmap((uint8_t*)buffer_vaddr_ + buffer_size, buffer_size,
                          PROT_READ | PROT_WRITE,
                          MAP_FIXED | MAP_SHARED | MAP_HUGETLB, buffer_fd, 0);

  if (ret == (void*)-1) {
    std::cerr << "(" << errno << ") Could not mmap second huge page"
              << std::endl;
    free(buffer_vaddr_);
    close(buffer_fd);
    assert(false);
  }

  if (mlock(buffer_vaddr_, buffer_size)) {
    std::cerr << "(" << errno << ") Could not lock huge page" << std::endl;
    munmap(buffer_vaddr_, buffer_size);
    close(buffer_fd);
    assert(false);
  }
  close(buffer_fd);

  // Initialize the other parameters
  free_capacity_ = buffer_size;
  buffer_paddr_ = virt_to_phys(buffer_vaddr_);
}

void* RingBufferMemoryAllocator::allocate(const uint64_t size) {
  if (unlikely(free_capacity_ < size)) {
    return nullptr;
  }  // OOM error
  void* buf = static_cast<uint8_t*>(buffer_vaddr_) + alloc_offset_;
  alloc_offset_ = (alloc_offset_ + size) % kMaxBufferSize;
  free_capacity_ -= size;  // Update capacity
  return buf;
}

void RingBufferMemoryAllocator::deallocate(const uint64_t size) {
  free_capacity_ += size;
  assert(likely(free_capacity_ <= kMaxBufferSize));
}

/**
 * TXPacketQueueManager implementation.
 */
void TXPacketQueueManager::initialize(const uint8_t idx, const int pd_ring_fd,
                                      const uint64_t pd_ring_size) {
  pd_allocators_[idx].initialize(pd_ring_fd, pd_ring_size);
}

void* TXPacketQueueManager::allocate(const uint8_t idx, const uint64_t size) {
  return pd_allocators_[idx].allocate(size);
}

void TXPacketQueueManager::deallocate(const uint8_t idx,
                                      const uint64_t num_bytes) {
  pd_allocators_[idx].deallocate(num_bytes);
}
