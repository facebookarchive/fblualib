/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <algorithm>
#include <cerrno>
#include <folly/Malloc.h>
#include <glog/logging.h>

extern "C" {

// NOTE: this must match the ffi.cdef in ffivector.lua

typedef struct {
  size_t elementSize;
  size_t size;
  size_t capacity;
  void* data;
} FFIVector;

int ffivector_create(FFIVector* v, size_t elementSize, size_t initialCapacity);
void ffivector_destroy(FFIVector* v);
int ffivector_reserve(FFIVector* v, size_t n);
int ffivector_resize(FFIVector* v, size_t n);

}

int ffivector_create(FFIVector* v, size_t elementSize, size_t initialCapacity) {
  v->elementSize = elementSize;
  v->size = 0;
  v->capacity = initialCapacity;
  if (initialCapacity != 0) {
    v->data = malloc(initialCapacity * elementSize);
    if (!v->data) {
      return -ENOMEM;
    }
  } else {
    v->data = nullptr;
  }
  return 0;
}

void ffivector_destroy(FFIVector* v) {
  free(v->data);
}

int ffivector_reserve(FFIVector* v, size_t n) {
  if (n <= v->capacity) {
    return 0;
  }

  try {
    if (v->data) {
      v->data = folly::smartRealloc(
          v->data,
          v->size * v->elementSize,
          v->capacity * v->elementSize,
          n * v->elementSize);
    } else {
      v->data = folly::checkedMalloc(n * v->elementSize);
    }
  } catch (const std::bad_alloc&) {
    return -ENOMEM;
  }

  v->capacity = malloc_usable_size(v->data) / v->elementSize;
  DCHECK_GE(v->capacity, n);
  return 0;
}

int ffivector_resize(FFIVector* v, size_t n) {
  if (n > v->capacity) {
    // 1.5x growth factor
    size_t newCapacity = std::max(n, 1 + v->size * 3 / 2);
    int r = ffivector_reserve(v, newCapacity);
    if (r != 0) {
      return r;
    }
  }

  if (n > v->size) {
    memset(static_cast<char*>(v->data) + v->size * v->elementSize,
           0,
           (n - v->size) * v->elementSize);
  }

  v->size = n;
  return 0;
}
