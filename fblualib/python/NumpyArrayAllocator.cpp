/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "NumpyArrayAllocator.h"

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include <thpp/Storage.h>

namespace fblualib {
namespace python {

void* NumpyArrayAllocator::malloc(long size) {
  return (*THDefaultAllocator.malloc)(nullptr, size);
}

void* NumpyArrayAllocator::realloc(void* ptr, long size) {
  if (array_ && ptr == PyArray_DATA(array())) {
    void* newPtr = this->malloc(size);
    memcpy(newPtr, ptr, std::min(size, PyArray_NBYTES(array())));
    // Whee! We're done!
    release();
    return newPtr;
  }
  return (*THDefaultAllocator.realloc)(nullptr, ptr, size);
}

void NumpyArrayAllocator::free(void* ptr) {
  // We're relying on the slightly unsafe (and undocumented) behavior that
  // THStorage will only call the "free" method of the allocator once at the
  // end of its lifetime.
  if (array() && ptr == PyArray_DATA(array())) {
    release();
    return;
  }
  (*THDefaultAllocator.free)(nullptr, ptr);
  delete this;
}

void NumpyArrayAllocator::release() {
  if (!array_) {
    return;
  }
  debugDeletePythonRef(array_.get());
  // We may or may not have a Python context, let's be safe.
  PythonGuard g;
  array_.reset();
}

int initNumpyArrayAllocator(lua_State* L) {
  initNumpy(L);
  return 0;
}

}}  // namespaces
