/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_UTILS_H_
#define FBLUA_PYTHON_UTILS_H_

#include <string>

#include <boost/noncopyable.hpp>
#include <boost/operators.hpp>

#include <lua.hpp>
#include <luaT.h>
#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include <glog/logging.h>

#include <thpp/Tensor.h>
#include "Debug.h"
#ifndef NDEBUG
#include <folly/ThreadLocal.h>
#endif

namespace fblualib {
namespace python {

class PythonGuard : private boost::noncopyable {
 public:
  PythonGuard() : gstate_(PyGILState_Ensure()) {
#ifndef NDEBUG
    ++*count_;
#endif
  }
  ~PythonGuard() {
    PyGILState_Release(gstate_);
#ifndef NDEBUG
    --*count_;
    assert(*count_ >= 0);
#endif
  }
  static void assertHeld() {
#ifndef NDEBUG
    assert(*count_ > 0);
#endif
  }
 private:
  PyGILState_STATE gstate_;
#ifndef NDEBUG
  static folly::ThreadLocal<int> count_;
#endif
};

// Format the current Python exception as a string.
std::string formatPythonException();

// Raise the current Python error as a Lua error
template <class... Args>
void raisePythonError(lua_State* L, Args&&... args);

// Check a condition; if false, raise the Python error as a lua error.
template <class Cond, class... Args>
void checkPythonError(Cond&& cond, lua_State* L, Args&&... args);

// Yes, static.
// The numpy C API is implemented using macros; PyArray_Foo is expanded
// to (*PyArray_API->foo), and PyArray_API is declared as static in a header
// file. This means it must be initialized (by calling import_array) in
// *every* file where it is used.
static int initNumpy(lua_State* L);

// Fix an index to be absolute (>0) rather than relative to the top of the
// stack (<0)
void fixIndex(lua_State* L, int& index);

// Handle around a PyObject that keeps track of one object reference.
// By default, does not increment the reference count when constructed
// from a PyObject*, to match the Python API convention that API functions
// return new references. You should construct PyObjectHandle objects
// immediately from the return value of a Python API function:
//
// PyObjectHandle list(PyList_New());
class PyObjectHandle : private boost::equality_comparable<PyObjectHandle> {
 public:
  PyObjectHandle() : obj_(nullptr) { }
  /* implicit */ PyObjectHandle(std::nullptr_t) noexcept : obj_(nullptr) { }

  // Construct, without incrementing obj's reference count
  explicit PyObjectHandle(PyObject* obj) noexcept : obj_(obj) { }

  enum IncRef { INCREF };
  // Construct, incrementing obj's reference count
  PyObjectHandle(IncRef, PyObject* obj) noexcept : obj_(obj) {
    PythonGuard::assertHeld();
    Py_XINCREF(obj_);
  }

  PyObjectHandle(const PyObjectHandle& other) noexcept : obj_(other.obj_) {
    PythonGuard::assertHeld();
    Py_XINCREF(obj_);
  }

  PyObjectHandle(PyObjectHandle&& other) noexcept : obj_(other.obj_) {
    other.obj_ = nullptr;
  }

  ~PyObjectHandle() noexcept {
    reset();
  }

  PyObjectHandle& operator=(const PyObjectHandle& other) noexcept {
    PythonGuard::assertHeld();
    if (this != &other) {
      // This is safe even if obj_ == other.obj_, that would imply that the
      // refcount is at least 2, so the Py_XDECREF leaves it at least 1.
      Py_XDECREF(obj_);
      obj_ = other.obj_;
      Py_XINCREF(obj_);
    }
    return *this;
  }

  PyObjectHandle& operator=(PyObjectHandle&& other) noexcept {
    if (this != &other) {
      if (obj_) {
        PythonGuard::assertHeld();
        // This is safe even if obj_ == other.obj_, that would imply that the
        // refcount is at least 2, so the Py_XDECREF leaves it at least 1.
        Py_DECREF(obj_);
      }
      obj_ = other.obj_;
      other.obj_ = nullptr;
    }
    return *this;
  }

  // Return the PyObject* contained in the handle
  PyObject* get() const noexcept { return obj_; }

  // Return the PyObject* contained in the handle and release all claim
  // to it (that is, we won't Py_DECREF it in the destructor); use it to pass
  // as argument to the (few) Python API functions that steal references.
  PyObject* release() noexcept {
    PyObject* o = obj_;
    obj_ = nullptr;
    return o;
  }

  // Reset to point to another object; do not increment obj's reference count
  void reset(PyObject* obj = nullptr) noexcept {
    if (obj_) {
      PythonGuard::assertHeld();
      Py_DECREF(obj_);
    }
    obj_ = obj;
  }

  // Reset to point to another object; increment obj's reference count
  void reset(IncRef, PyObject* obj) noexcept {
    PythonGuard::assertHeld();
    Py_XINCREF(obj);
    Py_XDECREF(obj_);
    obj_ = obj;
  }

  PyObject* operator->() const noexcept { return get(); }
  PyObject& operator*() const noexcept { assert(get()); return *get(); }

  explicit operator bool() const noexcept { return obj_; }

 private:
  PyObject* obj_;
};

// equality comparable
inline bool operator==(const PyObjectHandle& a, const PyObjectHandle& b)
  noexcept {
  return a.get() == b.get();
}

}}  // namespaces

#include "Utils-inl.h"

namespace std {

// std::hash specialization for PyObjectHandle
template <>
struct hash<::fblualib::python::PyObjectHandle> {
  size_t operator()(
      const ::fblualib::python::PyObjectHandle& obj)
      const {
    return std::hash<void*>()(obj.get());
  }
};

}  // namespace std

#endif /* FBLUA_PYTHON_UTILS_H_ */
