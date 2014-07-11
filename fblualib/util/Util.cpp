/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// folly/Portability.h defines clock_gettime on systems that don't have it
// (Hello, OSX)
#include <cerrno>
#include <ctime>
#include <glog/logging.h>
#include <folly/Portability.h>
#include <folly/Random.h>

namespace {
constexpr int64_t kNsPerUs = 1000;
constexpr int64_t kUsPerS = 1000000;

int64_t getMicroseconds(clockid_t clock) noexcept {
  struct timespec ts;
  CHECK_ERR(clock_gettime(clock, &ts));
  return int64_t(ts.tv_nsec) / kNsPerUs + int64_t(ts.tv_sec) * kUsPerS;
}

}  // namespace

extern "C" int64_t getMicrosecondsMonotonic() {
  return getMicroseconds(CLOCK_MONOTONIC);
}

extern "C" int64_t getMicrosecondsRealtime() {
  return getMicroseconds(CLOCK_REALTIME);
}

extern "C" void sleepMicroseconds(int64_t us) {
  struct timespec req;
  req.tv_sec = us / kUsPerS;
  req.tv_nsec = (us % kUsPerS) * kNsPerUs;
  struct timespec rem;

  while (nanosleep(&req, &rem) == -1) {
    PCHECK(errno == EINTR);
    req = rem;
  }
}

extern "C" uint32_t randomNumberSeed() {
  return folly::randomNumberSeed();
}
