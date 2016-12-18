/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "AtomicVector.h"
#include <gtest/gtest.h>

#include <unistd.h>

#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>

using namespace std;
using namespace fblualib;

// Torch tensors are a bit heavyweight to find real, CPU-level races.
// Let's do integers instead.
template<>
struct Refcount<int> {
  constexpr static int kMaxInt = 1000;
  static atomic<int> m_counts[kMaxInt];

  static void _check(int i) {
    // Reserve the value 0, because of puns in AtomicVector that
    // are meant to work with pointers.
    assert(i > 0 && i <= kMaxInt);
  }

  void inc(int i) {
    _check(i);
    m_counts[i - 1]++;
  }

  void dec(int i) {
    _check(i);
    assert(m_counts[i - 1] > 0);
    m_counts[i - 1]--;
  }

  int get(int i, bool debug = false) const {
    _check(i);
    return m_counts[i - 1];
  }

  void assertClear() const {
    for (int i = 0; i < kMaxInt; i++) {
      ASSERT_EQ(m_counts[i], 0);
    }
  }
};

atomic<int> Refcount<int>::m_counts[Refcount<int>::kMaxInt];

TEST(AtomicVector, append) {
  AtomicVector<int> vec;
  Refcount<int> rc;
  const int M = 100;
  for (int i = 0; i < M; i++) {
    vec.append(i * 3 + 1);
  }

  for (int i = 0; i < M; i++) {
    auto val = vec.read(i);
    assert(val == i * 3 + 1);
    rc.dec(val);
  }
}

TEST(AtomicVector, write) {
  AtomicVector<int> vec;
  Refcount<int> rc;
  // Some single-threaded write tests.
  bool sawExc = false;
  try {
    vec.write(1, 666); // Past end of vector
  } catch(runtime_error& re) {
    sawExc = true;
  }
  ASSERT_EQ(sawExc, true);

  // Append a few.
  const int N = 17;
  const int M = 1000;
  for (int i = 1; i < M; i++) {
    ASSERT_EQ(rc.get(i, true), 0);
  }
  for (int i = 0; i < N; i++) {
    vec.append(3 * i + 1);
  }

  for (int i = 0; i < N; i++) {
    ASSERT_EQ(rc.get(3 * i + 1), 1);
  }

  for (int i = 0; i < M; i++) {
    auto idx = i % N;
    vec.write(idx, i + 1);
  }

  for (int i = 0; i < N; i++) {
    auto val = vec.read(i);
    rc.dec(val);
    ASSERT_EQ(rc.get(val), 1);
  }
}

template<typename Lambda>
int mptest(Lambda l) {
  vector<thread> threads;
  auto nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  for (int i = 0; i < nprocs; i++) {
    threads.push_back(thread([l, i] {
      l(i);
    }));
  }

  for (auto& t: threads) {
    t.join();
  }
  return nprocs;
}

TEST(AtomicVector, mpAppend) {
  Refcount<int> rc;
  // No missed appends
  for (int i = 0; i < 12; i++) { // A few times
    ASSERT_EQ(rc.get(1, true), 0);
    AtomicVector<int> lval;
    const int M = 1000;
    auto numThreads = mptest([&](int idx) {
      for (int i = 0; i < M; i++) {
        lval.append(1);
      }
    });
    ASSERT_EQ(lval.size(), M * numThreads);
    ASSERT_EQ(rc.get(1), M * numThreads);
  }
  rc.assertClear();
}

TEST(AtomicVector, mpRefcount) {
  Refcount<int> rc;
  // Test refcount reasoning.
  for (int i = 0; i < 12; i++) {
    assert(rc.get(1, true) == 0);
    AtomicVector<int> lval;
    auto numThreads = mptest([&](int idx) {
        lval.append(1);
    });
    ASSERT_EQ(lval.size(), numThreads);
    assert(rc.get(1) == numThreads);

    (void) mptest([&](int idx) {
        lval.write(idx, idx + 1);
    });
    for (int i = 0; i < numThreads; i++) {
      ASSERT_EQ(rc.get(i + 1), 1);
    }
  }
  rc.assertClear();
}
