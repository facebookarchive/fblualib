/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <atomic>
#include <stdexcept>
#include <thread>

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>

#include <folly/Bits.h>

#include <fblualib/thrift/LuaObject.h>

template <typename T> class Refcount;
template <typename T> class Serde;

namespace fblualib {

namespace detail {
// A SensitiveSection is a region of code that multiple threads can
// be in, and for which other threads can wait until every other thread
// has been observed not to be in the SensitiveSection. This is not to
// be confused with mutual exclusion, or even RWlock semantics; nothing
// guarantees that other threads will not enter the sensitve section
// after a wait operation. Also beware that this provides no fairness
// guarantees to waiters; threads entering the sensitive section are
// given static priority.
class SensitiveSection {
  typedef std::atomic<uint32_t> AtomicCount;
public:
  explicit SensitiveSection(int n)
  : m_n(n)
  {
    m_counts = (AtomicCount*)calloc(sizeof(AtomicCount), m_n);
  }

  ~SensitiveSection() {
    free(m_counts);
  }

  void enter() {
    getCount()++;
  }

  void leave() {
    getCount()--;
  }

  void wait() {
    // Buyer beware: no fairness guarantees.
    for (int i = 0; i < m_n; i++)
      while (m_counts[i].load()) {
#ifdef __x86_64__
        asm("pause");
#else
#error "Use your the thread relaxation primitive for your architecture."
#endif
      }
  }

  // Note: racy (hence "appears"). For use in asserts where the
  // the system is in a known state.
  bool appearsFree() const {
    for (int i = 0; i < m_n; i++)
      if (m_counts[i].load()) return false;
    return true;
  }

private:
  AtomicCount& getCount() {
    auto self = (uintptr_t)pthread_self();
    // Knuth multiplicative hash. pthread_t is really a pointer,
    // and we want to spread it around the m_counts array somewhat.
    return m_counts[(self * 2654435761) % m_n];
  }

  // Implementation note: each thread gets its own count rather than
  // its own cacheline. The latter might be preferable, but is costly
  // in space.
  uint32_t m_n;
  AtomicCount* m_counts;
};

// RAII guard for SS.
struct SensitiveSectionGuard {
  explicit SensitiveSectionGuard(SensitiveSection* ss) : m_ss(ss) {
    m_ss->enter();
  }
  ~SensitiveSectionGuard() {
    m_ss->leave();
  }
  SensitiveSectionGuard& operator=(const SensitiveSectionGuard&) = delete;
  SensitiveSectionGuard(const SensitiveSectionGuard&) = delete;
private:
  SensitiveSection* m_ss;
};

}

// Vector-like container that can only grow, and can be randomly
// read and written from many threads in a mostly obstruction-free manner.
// Sizes are integers. 4 billion tensors should be enough for anybody.
//
// Assumptions about T:
//    - it's POD
//    - it has an invalid value that evaluates to boolean false (most
//      likely, it's a pointer)
//    - Refcount<T> knows how to manipulate refcounts, providing inc(),
//      dec(), and get() methods.
//    - atomic<T> exists.
//    - 0 can be implicitly converted to it.
//
// AtomicVector itself is ref-counted. We bump its refcount when
// handing out Lua references, and decrement it when lua GC's one.
template<typename T>
class AtomicVector {
  typedef uint32_t BucketIndex;

  // Buckets are the linear containers hanging off the logical
  // collection's spine in exponentially increasing sizes.
  struct Bucket {
    explicit Bucket(size_t capac)
#ifndef NDEBUG
    : m_capac(capac)
#endif
    {
      m_items = static_cast<std::atomic<T>*>
        (calloc(sizeof(std::atomic<T>), capac));
    }

    ~Bucket() {
      free(m_items);
    }

    Bucket& operator=(const Bucket&) = delete;
    Bucket(const Bucket&) = delete;

    T operator[](size_t slot) const {
      assert(slot < m_capac);
      return m_items[slot].load();
    }

    bool cmpxchg(size_t slot, T exp, T desired) {
      assert(slot < m_capac);
      return m_items[slot].compare_exchange_weak(exp, desired);
    }

    std::atomic<T>& getAtomic(int slot) {
      assert(slot < m_capac);
      return m_items[slot];
    }

    std::atomic<T>* m_items;
#ifndef NDEBUG
    size_t m_capac;
#endif
  };

 public:
  AtomicVector()
  : m_size(0)
  , m_sensitiveSection(128)
  {
    for (BucketIndex i = 0; i < kMaxBuckets; i++) {
      m_buckets[i].store(nullptr);
    }
  }

  ~AtomicVector() {
    // Decref everything in the table. Presumably, if we're destroying
    // the table, the caller knows that it is no longer reachable, so
    // don't bother with the sensitive section.
    assert(m_sensitiveSection.appearsFree());
    Refcount<T> rc;
    for (BucketIndex i = 0; i < m_size; i++) {
      // Could be a bit more efficient. Is destroying AtomicVector's a
      // common operation?
      auto val = read(i);
      rc.dec(val); // Once for the read
      rc.dec(val); // Once because the table's gone
    }
    for (int i = 0; i < kMaxBuckets; i++) {
      delete m_buckets[i].load();
    }
  }

  // append: returns with the value appended to the end of the
  // vector. Since our vector can only grow, the semantic here is
  // reasonably well-posed. Returns position of the value in the
  // vector.
  size_t append(T val) {
  restart:
    auto insertionPoint = m_size.load();
    auto& bucket = indexToBucket(insertionPoint);
    auto buck = bucket.load();
    if (!buck) {
      // Bucket allocation. Some set of inserters might be racing
      // through the following code; tread with caution.
      auto sz = 1 << (&bucket - &m_buckets[0]);
      auto old = buck;
      buck = new Bucket(sz);
      if (!bucket.compare_exchange_weak(old, buck)) {
        delete buck;
        goto restart;
      }
    }
    // We can't get here if this bucket is null.
    assert(buck);
    // The bucket can't move.
    assert(buck == bucket.load());

    // OK! We know where we'd like to put this item, *and* memory that
    // won't be going anywhere backs it. Entities we might race with
    // include:
    //   0. Callers to write(). We won't do a random-access-style write
    //      to this slot yet, because m_size doesn't yet recognize the
    //      presence of this slot. See the code in write() that reroutes
    //      writes to the m_size slot through append.
    //
    //   1. Other callers to append. It's possible several writers are
    //      attempting to append into the same slot. Here we rely on calloc
    //      producing a unique nullptr-like representation that can never
    //      be inserted again (since we take a reference here, and
    //      references are never to null). If we fail the race to write
    //      this slot, we restart.
    auto indexInBucket = indexToIntraBucketIndex(insertionPoint,
                                                 &bucket - &m_buckets[0]);
    if (!buck->cmpxchg(indexInBucket, 0, val)) {
      goto restart;
    }

    // We've written the slot. No other callers to append will do so,
    // because it's not null so the compare_exchange_weak will fail.
    Refcount<T> rc;
    rc.inc(val);

    // Bump m_size, making this slot visible and completing the append
    // operation.
    //
    // Subtlety: do we really know m_size == insertionPoint? The algorithm
    // is *really* broken otherwise; readers could see val before we incref
    // it.

    assert(m_size.load() == insertionPoint);

    // Yes, m_size == insertionPoint. Racers through this code are
    // invariably trying to CAS the slot at m_size from nullptr to
    // something else, and only one can win that race. If multiple
    // inserters race, the cas from nullptr will keep failing until the
    // winner of that race bumps m_size here.  Upshot: this algorithm isn't
    // quite (lock,wait,obstruction)-free; inserters essentially "lock" the
    // right to insert by cas'ing that slot.
    m_size.fetch_add(1);
    return insertionPoint;
  }

  T read(BucketIndex slot) const {
    if (slot >= m_size) {
      throw std::runtime_error("read past end of vector");
    }

    auto& bucket = indexToBucket(slot);
    auto bidx = indexToIntraBucketIndex(slot, &bucket - &m_buckets[0]);

    Refcount<T> rc;
    detail::SensitiveSectionGuard ssg(&m_sensitiveSection);
    auto val = (*bucket.load())[bidx];
    rc.inc(val);
    return val;
  }

  void write(BucketIndex slot, T val) {
    assert(val);
    if (slot >= m_size) {
      throw std::runtime_error("write past end of vector; use vec:append()?");
    }

    Refcount<T> rc;
    rc.inc(val);
    auto& bucketP = indexToBucket(slot);
    auto bidx = indexToIntraBucketIndex(slot, &bucketP - &m_buckets[0]);
  restart:
    auto& home = (bucketP.load())->getAtomic(bidx);
    auto old = home.load();
    if (!home.compare_exchange_weak(old, val)) goto restart;
    // We've succeeded. If we just swapped out an old value, don't decref
    // it yet; wait to make sure nobody is in the process of
    // reading-and-increffing it.
    if (old) {
      m_sensitiveSection.wait();
      rc.dec(old);
    }
  }

  size_t size() const {
    return m_size.load();
  }

  template<typename Lambda, typename Datum>
  static void fileOp(Lambda l, Datum* data, size_t nData, FILE* file) {
    size_t nFrobbed = l(data, sizeof(Datum), nData, file);
    if (nFrobbed != nData) {
      throw std::runtime_error("file operation failed");
    }
  }

  template<typename Lambda, typename Datum>
  static void fileOp(Lambda l, Datum* data, FILE* file) {
    fileOp(l, data, 1, file);
  }

  void load(FILE* file) {
    int magic;
    size_t sz;
    fileOp(fread, &magic, file);
    if (magic != 0x04081977) {
      throw std::runtime_error("bad magic value loading atomicvec");
    }
    fileOp(fread, &sz, file);
    std::vector<size_t> directory(sz);
    fileOp(fread, &directory[0], sz, file);

    growUnsafe(sz);
    auto nThreads = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<std::thread> deserThreads;
    // Get the UNIX fd for pread. We'll seek the FILE* manually later to
    // fit caller expectations.
    int fd = fileno(file);
    fflush(file);
    size_t finalFilePtr;
    for (int tid = 0; tid < nThreads; tid++) {
      deserThreads.emplace_back([tid, sz, fd, nThreads, this,
                                &directory, &finalFilePtr] {
        auto safe_pread = [&](int fd, void* dest, size_t sz, off_t off) {
          auto retval = pread(fd, dest, sz, off);
          if (retval != sz) {
            auto msg = folly::format("pread failed: {}", strerror(errno));
            throw std::runtime_error(msg.str());
          }
        };
        std::vector<uint8_t> bytes(1 << 20);
        // Consume the file in a breadth-first fashion; thread 0 is decoding
        // item 0 while thread 1 is decoding item 1. This way we plow through
        // the file in roughly sequential order.
        for (size_t i = tid; i < sz; i += nThreads) {
          size_t entrySz;
          safe_pread(fd, &entrySz, sizeof(entrySz), directory[i]);
          if (entrySz > bytes.size()) bytes.resize(entrySz);
          safe_pread(fd, &bytes[0], entrySz, directory[i] + sizeof(entrySz));
          if (i == sz - 1) {
            finalFilePtr = directory[i] + entrySz + sizeof(entrySz);
          }
          folly::ByteRange range(&bytes[0], entrySz);
          try {
            write(i, Serde<T>::load(&range));
          } catch(std::runtime_error& e) {
            fprintf(stderr, "hmm, could not deserialize: %s\n", e.what());
            fprintf(stderr, "at dir %zd tid %d entry length %zd offset %zd\n",
                   i, tid, entrySz, directory[i]);
            throw;
          }
        }
      });
    }

    // Clean up threads and the file pointer.
    for (auto& t: deserThreads) t.join();
    fseek(file, finalFilePtr, SEEK_SET);
  }

  // save() is inherently racy; if other threads are still appending we may miss
  // new entries, but all vectors visible from the calling thread's timeline
  // will be serialized.
  void save(FILE* file) const {
    const int kMagic = 0x04081977;
    fileOp(fwrite, &kMagic, file);

    size_t sz = size();
    fileOp(fwrite, &sz, file);

    // Next up is a directory of file offsets.
    std::vector<size_t> offsets(sz);
    size_t directoryOff = ftell(file);
    fseek(file, sz * sizeof(size_t), SEEK_CUR);
    size_t dataStart = ftell(file);
    size_t i;
    for (i = 0; i < sz; i++) {
      auto val = read(i);
      SCOPE_EXIT {
        Refcount<T>().dec(val);
      };
      offsets[i] = ftell(file);
      fblualib::thrift::StringWriter sw;
      auto str = Serde<T>::save(val, sw);
      auto strsz = str.size();
      fileOp(fwrite, &strsz, file);
      fileOp(fwrite, str.data(), strsz, file);
    }
    CHECK(i == sz);
    size_t end = ftell(file);
    fseek(file, directoryOff, SEEK_SET);
    fileOp(fwrite, &offsets[0], offsets.size(), file);
    assert(ftell(file) == dataStart);
    fseek(file, end, SEEK_SET);
  }

 protected:
  static const BucketIndex kMaxBuckets = 32;
  std::atomic<Bucket*> m_buckets[kMaxBuckets];
  std::atomic<BucketIndex> m_size;
  mutable detail::SensitiveSection m_sensitiveSection;

  static int highOrderBit(BucketIndex val) {
    return folly::findLastSet(val);
  }

  static int indexToBucketIndex(BucketIndex index) {
    return highOrderBit(index + 1) - 1; // sic
  }

  const std::atomic<Bucket*>& indexToBucket(BucketIndex index) const {
    return m_buckets[indexToBucketIndex(index)];
  }

  std::atomic<Bucket*>& indexToBucket(BucketIndex index) {
    return m_buckets[indexToBucketIndex(index)];
  }

  BucketIndex indexToIntraBucketIndex(BucketIndex index, int bucket) const {
    auto bucketStart = (1 << bucket) - 1;
    auto bucketEnd = (1 << (bucket + 1)) - 1;
    assert(index >= bucketStart);
    assert(index < bucketEnd);
    return index - bucketStart;
  }

 private:
  void growUnsafe(size_t size) {
    assert(m_size.load() == 0);
    m_size.store(size);
    auto targetBucketIndex = indexToBucketIndex(size);
    for (int i = 0; i <= targetBucketIndex; i++) {
      assert(!m_buckets[i].load());
      m_buckets[i].store(new Bucket(1 << i));
    }
  }
};

} // namespace fblualib
