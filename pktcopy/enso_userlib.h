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

#ifndef PKTCOPY_ENSO_USERLIB_H_
#define PKTCOPY_ENSO_USERLIB_H_

#include <cstddef>
#include <cstdint>
#include <string>

// Macro to generate default methods
#define DEFAULT_CTOR_AND_DTOR(TypeName) \
  TypeName() = default;                 \
  ~TypeName() = default

// Macro to disallow copy/assignment
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete

// TODO(natre): Don't hardcode these parameters
static constexpr uint16_t kLogCacheLineSize = 6;
static constexpr size_t kHugePageSize = (2048 * 1024);
static constexpr uint64_t kMaxBufferSize = kHugePageSize;

static constexpr uint16_t kCacheLineAlignmentMask =
    (((0xffff) >> kLogCacheLineSize) << kLogCacheLineSize);
static constexpr uint16_t kCacheLineSize = (1 << kLogCacheLineSize);

template <typename T>
T round_to_cache_line_size(const T req_size) {
  return (T)((req_size + kCacheLineSize - 1) & kCacheLineAlignmentMask);
}

/**
 * Implementation of a memory allocator that uses a fixed-size ringbuffer
 * as the underlying memory resource. Requires memory to be free'd in the
 * order that it is allocated. Not thread-safe.
 */
class RingBufferMemoryAllocator {
 private:
  void* buffer_vaddr_ = nullptr;  // Double-mapped virtual addrspace
  uint64_t buffer_paddr_ = 0;     // Base paddr of the ringbuffer

  // Housekeeping
  uint64_t alloc_offset_ = 0;   // Allocation offset
  uint64_t free_capacity_ = 0;  // Available memory

 public:
  DEFAULT_CTOR_AND_DTOR(RingBufferMemoryAllocator);

  /**
   * Initialize the memory allocator.
   */
  void initialize(const int buffer_fd, const uint64_t buffer_size);

  /**
   * Allocates a contiguous region in memory. If the
   * request cannot be satisfied, returns nullptr.
   */
  void* allocate(const uint64_t size);

  /**
   * Deallocates blocks of memory. Memory MUST be dealloc'd
   * in the same order that it was originally allocated.
   */
  void deallocate(const uint64_t size);

  /**
   * Returns the physical address corresponding to the
   * current alloc offset.
   */
  inline uint64_t get_alloc_paddr() const {
    return buffer_paddr_ + alloc_offset_;
  }

  DISALLOW_COPY_AND_ASSIGN(RingBufferMemoryAllocator);
};

/**
 * Helper class to manage a socket's TX queue.
 */
class TXPacketQueueManager final {
 public:
  static constexpr uint8_t kNumAllocators = 16;

 private:
  RingBufferMemoryAllocator pd_allocators_[kNumAllocators]{};

 public:
  DEFAULT_CTOR_AND_DTOR(TXPacketQueueManager);

  // Accessors
  inline const RingBufferMemoryAllocator& get_allocator(const uint8_t idx) {
    return pd_allocators_[idx];
  }

  /**
   * Initialize the TX packet queue.
   */
  void initialize(const uint8_t idx, const int pd_ring_fd,
                  const uint64_t pd_ring_size);

  /**
   * Returns a memory region with the given size.
   */
  void* allocate(const uint8_t idx, const uint64_t size);

  /**
   * Updates the packet queue state on TX events.
   */
  void deallocate(const uint8_t idx, const uint64_t num_bytes);

  DISALLOW_COPY_AND_ASSIGN(TXPacketQueueManager);
};

#endif  // PKTCOPY_ENSO_USERLIB_H_
