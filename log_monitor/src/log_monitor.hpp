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

#ifndef LOG_MONITOR_SRC_LOG_MONITOR_HPP_
#define LOG_MONITOR_SRC_LOG_MONITOR_HPP_

#include <hs/hs.h>
#include <rte_common.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_fbk_hash.h>
#include <rte_ip.h>
#include <rte_jhash.h>
#include <rte_vect.h>

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "build_regex_database.hpp"

static int on_match(unsigned int, unsigned long long, unsigned long long,
                    unsigned int, void* context) {
  uint64_t* match_count = static_cast<uint64_t*>(context);
  (*match_count)++;
  return 0;
}

/**
 * @brief Log Monitor.
 *
 * Aggregates logs from multiple nodes and uses hyperscan to scan for patterns.
 */
class LogMonitor {
 public:
  explicit LogMonitor(const std::string& regex_filename,
                      const uint32_t nb_streams) noexcept
      : kRegexFilename(regex_filename),
        kNbStreams(nb_streams),
        match_count_(0),
        db_streaming_(nullptr),
        db_block_(nullptr),
        scratch_(nullptr) {}

  LogMonitor(LogMonitor&&) = default;
  LogMonitor& operator=(LogMonitor&&) = default;

  LogMonitor(const LogMonitor&) = delete;
  LogMonitor& operator=(const LogMonitor&) = delete;

  ~LogMonitor() noexcept {
    hs_error_t ret;
    for (uint32_t i = 0; i < kNbStreams; ++i) {
      if (scratch_ == nullptr) {
        ret = hs_close_stream(streams_[i], nullptr, nullptr, nullptr);
      } else {
        ret = hs_close_stream(streams_[i], scratch_, on_match, &match_count_);
      }
      if (unlikely(ret != HS_SUCCESS)) {
        std::terminate();
      }
    }

    if (db_streaming_ != nullptr) {
      ret = hs_free_database(db_streaming_);
      if (unlikely(ret != HS_SUCCESS)) {
        std::terminate();
      }
    }

    if (db_block_ != nullptr) {
      ret = hs_free_database(db_block_);
      if (unlikely(ret != HS_SUCCESS)) {
        std::terminate();
      }
    }

    if (scratch_ != nullptr) {
      ret = hs_free_scratch(scratch_);
      if (unlikely(ret != HS_SUCCESS)) {
        std::terminate();
      }
    }
  }

  /**
   * @brief Setup hyperscan. Must call after constructor.
   */
  int setup() {
    hs_error_t err;
    int ret = databasesFromFile(kRegexFilename, &db_streaming_, &db_block_);
    if (ret != 0) {
      return -1;
    }

    for (uint32_t i = 0; i < kNbStreams; ++i) {
      hs_stream_t* stream = nullptr;
      err = hs_open_stream(db_streaming_, 0, &stream);
      streams_.push_back(stream);
      if (unlikely(err != HS_SUCCESS)) {
        return -2;
      }
    }

    err = hs_alloc_scratch(db_streaming_, &scratch_);
    if (unlikely(err != HS_SUCCESS)) {
      return -3;
    }

    // Increase scratch size if required by block mode.
    err = hs_alloc_scratch(db_block_, &scratch_);
    if (unlikely(err != HS_SUCCESS)) {
      return -4;
    }

    return 0;
  }

  /**
   * @brief Scan buffer for patterns.
   *
   * @param buf Buffer to scan.
   * @param len Length of buffer.
   * @return Number of matches. Return -1 if error.
   */
  __rte_always_inline int lookup(const uint8_t* buffer, const uint32_t len,
                                 const uint32_t stream_id) {
    uint64_t count = 0;
    hs_error_t ret = hs_scan_stream(streams_[stream_id],
                                    reinterpret_cast<const char*>(buffer), len,
                                    0, scratch_, on_match, &count);
    match_count_ += count;
    if (unlikely(ret != HS_SUCCESS)) {
      return -1;
    }

    return count;
  }

  uint64_t get_match_count() const { return match_count_; }

 private:
  const std::string kRegexFilename;
  const uint32_t kNbStreams;
  uint64_t match_count_;
  hs_database_t *db_streaming_, *db_block_;
  hs_scratch_t* scratch_;
  std::vector<hs_stream_t*> streams_;
};

#endif  // LOG_MONITOR_SRC_LOG_MONITOR_HPP_
