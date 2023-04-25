/*
 * Copyright (c) 2015-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Adapted from Hyperscan example (pcapscan.cc).
 */

#include "build_regex_database.hpp"

#include <hs/hs.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Simple timing class
class Clock {
 public:
  void start() { time_start = std::chrono::system_clock::now(); }

  void stop() { time_end = std::chrono::system_clock::now(); }

  double seconds() const {
    std::chrono::duration<double> delta = time_end - time_start;
    return delta.count();
  }

 private:
  std::chrono::time_point<std::chrono::system_clock> time_start, time_end;
};

static hs_database_t *buildDatabase(
    const std::vector<const char *> &expressions,
    const std::vector<unsigned> flags, const std::vector<unsigned> ids,
    unsigned int mode, const bool verbose = true) {
  hs_database_t *db;
  hs_compile_error_t *compileErr;
  hs_error_t err;

  Clock clock;
  clock.start();

  err = hs_compile_multi(expressions.data(), flags.data(), ids.data(),
                         expressions.size(), mode, nullptr, &db, &compileErr);

  clock.stop();

  if (err != HS_SUCCESS) {
    if (compileErr->expression < 0) {
      // The error does not refer to a particular expression.
      std::cerr << "ERROR: " << compileErr->message << std::endl;
    } else {
      std::cerr << "ERROR: Pattern '" << expressions[compileErr->expression]
                << "' failed compilation with error: " << compileErr->message
                << std::endl;
    }
    // As the compileErr pointer points to dynamically allocated memory, if we
    // get an error, we must be sure to release it. This is not necessary when
    // no error is detected.
    hs_free_compile_error(compileErr);
    return nullptr;
  }

  if (verbose) {
    std::cout << "Hyperscan "
              << (mode == HS_MODE_STREAM ? "streaming" : "block")
              << " mode database compiled in " << clock.seconds() << " seconds."
              << std::endl;
  }

  return db;
}

// Adapted from Hyperscan example (pcapscan.cc).
static unsigned parseFlags(const std::string &flagsStr) {
  unsigned flags = HS_FLAG_PREFILTER;  // Always use PREFILTER.
  for (const auto &c : flagsStr) {
    switch (c) {
      case 'i':
        flags |= HS_FLAG_CASELESS;
        break;
      case 'm':
        flags |= HS_FLAG_MULTILINE;
        break;
      case 's':
        flags |= HS_FLAG_DOTALL;
        break;
      case 'H':
        flags |= HS_FLAG_SINGLEMATCH;
        break;
      case 'V':
        flags |= HS_FLAG_ALLOWEMPTY;
        break;
      case '8':
        flags |= HS_FLAG_UTF8;
        break;
      case 'W':
        flags |= HS_FLAG_UCP;
        break;
      case '\r':  // stray carriage-return
        break;
      default:
        std::cerr << "Unsupported flag \'" << c << "\'" << std::endl;
        return 0;
    }
  }
  return flags;
}

static int parseFile(const char *filename, std::vector<std::string> &patterns,
                     std::vector<unsigned> &flags, std::vector<unsigned> &ids) {
  std::ifstream inFile(filename);
  if (!inFile.good()) {
    std::cerr << "ERROR: Can't open pattern file \"" << filename << "\""
              << std::endl;
    return -1;
  }

  for (unsigned i = 1; !inFile.eof(); ++i) {
    std::string line;
    getline(inFile, line);

    // if line is empty, or a comment, we can skip it
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // otherwise, it should be ID:PCRE, e.g., 10001:/foobar/is

    size_t colonIdx = line.find_first_of(':');
    if (colonIdx == std::string::npos) {
      std::cerr << "ERROR: Could not parse line " << i << std::endl;
      return -1;
    }

    // we should have an unsigned int as an ID, before the colon
    unsigned id = std::stoi(line.substr(0, colonIdx).c_str());

    // rest of the expression is the PCRE
    const std::string expr(line.substr(colonIdx + 1));

    size_t flagsStart = expr.find_last_of('/');
    if (flagsStart == std::string::npos) {
      std::cerr << "ERROR: no trailing '/' char" << std::endl;
      return -2;
    }

    std::string pcre(expr.substr(1, flagsStart - 1));
    std::string flagsStr(expr.substr(flagsStart + 1, expr.size() - flagsStart));

    unsigned flag = parseFlags(flagsStr);
    if (flag == 0) {
      return -3;
    }
    // flag = 0;

    patterns.push_back(pcre);
    flags.push_back(flag);
    ids.push_back(id);
  }

  return 0;
}

/**
 * This function will read in the file with the specified name, with an
 * expression per line, ignoring lines starting with '#' and build a Hyperscan
 * database for it.
 */
int databasesFromFile(const std::string &filename, hs_database_t **db_streaming,
                      hs_database_t **db_block) {
  // hs_compile_multi requires three parallel arrays containing the patterns,
  // flags and ids that we want to work with. To achieve this we use vectors and
  // new entries onto each for each valid line of input from the pattern file.
  std::vector<std::string> patterns;
  std::vector<unsigned> flags;
  std::vector<unsigned> ids;

  // do the actual file reading and string handling
  int ret = parseFile(filename.c_str(), patterns, flags, ids);
  if (ret != 0) {
    return -1;
  }

  // Turn our vector of strings into a vector of char*'s to pass in to
  // hs_compile_multi. (This is just using the vector of strings as dynamic
  // storage.)
  std::vector<const char *> cstrPatterns;
  for (const auto &pattern : patterns) {
    cstrPatterns.push_back(pattern.c_str());
  }

  std::cout << "Compiling Hyperscan databases with " << patterns.size()
            << " patterns." << std::endl;

  *db_streaming = buildDatabase(cstrPatterns, flags, ids, HS_MODE_STREAM);
  if (!*db_streaming) {
    return -2;
  }

  *db_block = buildDatabase(cstrPatterns, flags, ids, HS_MODE_BLOCK);
  if (!*db_block) {
    return -3;
  }

  return 0;
}
