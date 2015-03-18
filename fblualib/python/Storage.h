/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_STORAGE_H_
#define FBLUA_PYTHON_STORAGE_H_

#include <thpp/Storage.h>
#include "Utils.h"

namespace fblualib {
namespace python {

// PythonStorage is a Python object that wraps a Storage (holding a reference
// as long as the Python reference count is >0). It also implements the
// buffer interface, but there's currently no use for that, as numpy arrays
// converted using PyArray_FromBuffer don't support strides.
template <class T>
class PythonStorage {
 public:
  PyObject_HEAD

  static PyObjectHandle allocate(lua_State* L, thpp::Storage<T> s) {
    // Allocate memory the Python way
    PyObject* selfObj = (*pythonType.tp_alloc)(&pythonType, 1);
    checkPythonError(selfObj, L, "allocate PythonStorage object");
    debugAddPythonRef(selfObj);

    // Create object with placement new
    new (selfObj) PythonStorage(std::move(s));
    return PyObjectHandle(selfObj);
  }

  ~PythonStorage() {
#ifndef NDEBUG
    if (storage_.data()) {
      debugDeleteLuaRef(storage_.data());
    }
#endif
  }

  // Define type for Python; for built-in types, this is called from
  // initStorage();
  static void define();

 private:
  static PyTypeObject pythonType;
  static PyBufferProcs pythonBufferProcs;

  explicit PythonStorage(thpp::Storage<T> s) : storage_(std::move(s)) {
#ifndef NDEBUG
    if (storage_.data()) {
      debugAddLuaRef(storage_.data());
    }
#endif
  }

  // Buffer interface
  static Py_ssize_t getBuffer(PyObject* selfObj, Py_ssize_t segment,
                              void** ptrptr);
  static Py_ssize_t segCount(PyObject* selfObj, Py_ssize_t* lenp);

  thpp::Storage<T> storage_;
  static void deallocate(PyObject* obj);

  static inline PythonStorage* self(PyObject* obj) {
    return reinterpret_cast<PythonStorage*>(obj);
  }
};

int initStorage(lua_State* L);

}}  // namespaces

#include "Storage-inl.h"

#endif /* FBLUA_PYTHON_STORAGE_H_ */
