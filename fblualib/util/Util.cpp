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
#include <mutex>
#include <unordered_set>
#include <glog/logging.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <folly/ThreadLocal.h>
#include <fblualib/CrossThreadRegistry.h>

// We may be running in a program that embeds google-glog, or we may not.
// If we don't, we still need to call InitGoogleLogging(), but
// InitGoogleLogging() abort()s if called twice. So we'll have to call
// this internal function to check.
namespace google { namespace glog_internal_namespace_ {
bool IsGoogleLoggingInitialized();
}}  // namespaces

namespace {
constexpr int64_t kNsPerUs = 1000;
constexpr int64_t kUsPerS = 1000000;

folly::ThreadLocal<std::string> errBuffer;

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
    *errBuffer = e.what();
    return errBuffer->data();
  }
}

const char* cUnescape(const char* str, size_t len, std::string* out) {
  try {
    folly::cUnescape(folly::StringPiece(str, len), *out);
    return nullptr;
  } catch (const std::exception& e) {
    *errBuffer = e.what();
    return errBuffer->data();
  }
}

namespace {
struct OnceRecord {
  OnceRecord() : called(false) { }
  std::mutex mutex;
  bool called;
};

CrossThreadRegistry<std::string, OnceRecord> gOnceRegistry;
CrossThreadRegistry<std::string, std::mutex> gMutexRegistry;
}  // namespace

OnceRecord* getOnce(const char* key) {
  try {
    return gOnceRegistry.getOrCreate(
        key,
        [] { return folly::make_unique<OnceRecord>(); });
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

bool lockOnce(OnceRecord* r) {
  r->mutex.lock();
  if (r->called) {
    r->mutex.unlock();
    return false;
  }
  return true;
}

void unlockOnce(OnceRecord* r, bool success) {
  assert(!r->called);
  r->called = success;
  r->mutex.unlock();
}

std::mutex* getMutex(const char* key) {
  try {
    return gMutexRegistry.getOrCreate(
        key,
        [] { return folly::make_unique<std::mutex>(); });
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

void lockMutex(std::mutex* mutex) {
  mutex->lock();
}

void unlockMutex(std::mutex* mutex) {
  mutex->unlock();
}

namespace {
// Must match order and count in logging.lua
const int gSeverities[] = {
  google::GLOG_INFO,
  google::GLOG_WARNING,
  google::GLOG_ERROR,
  google::GLOG_FATAL,
};
constexpr int gNumSeverities = sizeof(gSeverities) / sizeof(gSeverities[0]);
}  // namespace

void luaLog(int severity, const char* file, int line, const char* msg) {
  if (severity < 0) {
    severity = 0;
  } else if (severity >= gNumSeverities) {
    severity = gNumSeverities - 1;
  }
  google::LogMessage(file, line, gSeverities[severity]).stream() << msg;
}

namespace {

std::mutex gLoggingInitMutex;

}  // namespace

void luaInitLogging(const char* argv0) {
  std::lock_guard<std::mutex> lock(gLoggingInitMutex);
  if (!google::glog_internal_namespace_::IsGoogleLoggingInitialized()) {
    google::InitGoogleLogging(argv0 ? argv0 : "");
  }
}

}  // extern "C"
