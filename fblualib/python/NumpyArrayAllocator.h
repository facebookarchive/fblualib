/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_NUMPYARRAYALLOCATOR_H_
#define FBLUA_PYTHON_NUMPYARRAYALLOCATOR_H_

#include "Utils.h"

namespace fblualib {
namespace python {

// Torch "allocator" that ensures that a given numpy array keeps a reference
// alive until Torch no longer needs the memory.
class NumpyArrayAllocator {
 public:
  explicit NumpyArrayAllocator(PyObjectHandle a) : array_(std::move(a)) {
    debugAddPythonRef(array_.get());
  }
  ~NumpyArrayAllocator() { release(); }

  void* malloc(long size);
  void* realloc(void* ptr, long size);
  void free(void* ptr);

 private:
  void release();
  inline PyArrayObject* array() const {
    return reinterpret_cast<PyArrayObject*>(array_.get());
  }
  PyObjectHandle array_;
};

int initNumpyArrayAllocator(lua_State* L);

}}  // namespaces

#endif /* FBLUA_PYTHON_NUMPYARRAYALLOCATOR_H_ */
