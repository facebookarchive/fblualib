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
#include <folly/String.h>

namespace {
constexpr int64_t kNsPerUs = 1000;
constexpr int64_t kUsPerS = 1000000;

int64_t getMicroseconds(clockid_t clock) noexcept {
  struct timespec ts;
  CHECK_ERR(clock_gettime(clock, &ts));
  return int64_t(ts.tv_nsec) / kNsPerUs + int64_t(ts.tv_sec) * kUsPerS;
}

}  // namespace

extern "C" {

int64_t getMicrosecondsMonotonic() {
  return getMicroseconds(CLOCK_MONOTONIC);
}

int64_t getMicrosecondsRealtime() {
  return getMicroseconds(CLOCK_REALTIME);
}

void sleepMicroseconds(int64_t us) {
  struct timespec req;
  req.tv_sec = us / kUsPerS;
  req.tv_nsec = (us % kUsPerS) * kNsPerUs;
  struct timespec rem;

  while (nanosleep(&req, &rem) == -1) {
    PCHECK(errno == EINTR);
    req = rem;
  }
}

uint32_t randomNumberSeed() {
  return folly::randomNumberSeed();
}

std::string* stdStringNew() {
  try {
    return new std::string;
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

std::string* stdStringNewFromString(const char* s, size_t n) {
  try {
    return new std::string(s, n);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

std::string* stdStringClone(const std::string *s) {
  try {
    return new std::string(*s);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

void stdStringDelete(std::string* obj) {
  delete obj;
}

void stdStringClear(std::string* obj) {
  obj->clear();
}

const char* stdStringData(const std::string* s) {
  return s->data();
}

size_t stdStringSize(const std::string* s) {
  return s->size();
}

bool stdStringAppend(std::string* obj, const char* s, size_t n) {
  try {
    obj->append(s, n);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool stdStringAppendS(std::string* dest, const std::string* src) {
  try {
    dest->append(*src);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool stdStringInsert(std::string* dest, size_t pos, const char* str,
                     size_t len) {
  try {
    dest->insert(pos, str, len);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool stdStringInsertS(std::string* dest, size_t pos, const std::string* str) {
  try {
    dest->insert(pos, *str);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool stdStringReplace(std::string* dest, size_t pos, size_t n,
                      const char* str, size_t len) {
  try {
    dest->replace(pos, n, str, len);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool stdStringReplaceS(std::string* dest, size_t pos, size_t n,
                       const std::string* str) {
  try {
    dest->replace(pos, n, *str);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

void stdStringErase(std::string* dest, size_t pos, size_t n) {
  dest->erase(pos, n);
}

const char* cEscape(const char* str, size_t len, std::string* out) {
  try {
    folly::cEscape(folly::StringPiece(str, len), *out);
    return nullptr;
  } catch (const std::exception& e) {
    return e.what();
  }
}

const char* cUnescape(const char* str, size_t len, std::string* out) {
  try {
    folly::cUnescape(folly::StringPiece(str, len), *out);
    return nullptr;
  } catch (const std::exception& e) {
    return e.what();
  }
}

}  // extern "C"
