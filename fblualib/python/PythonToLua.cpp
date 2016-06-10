/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "PythonToLua.h"
#include "NumpyArrayAllocator.h"
#include "Ref.h"

namespace fblualib {
namespace python {

namespace {

template <class T>
void luaPushTensor(lua_State* L, thpp::TensorPtr<thpp::Tensor<T>> tensor) {
  luaT_pushudata(L, tensor.moveAsTH(), thpp::Tensor<T>::kLuaTypeName);
}
template <class T>
void luaPushTensor(lua_State* L, const thpp::Tensor<T>& tensor) {
  luaPushTensor<T>(L, tensor.copyPtr());
}

THAllocator numpyArrayTHAllocator = {
  &thpp::THAllocatorWrapper<NumpyArrayAllocator>::malloc,
  &thpp::THAllocatorWrapper<NumpyArrayAllocator>::realloc,
  &thpp::THAllocatorWrapper<NumpyArrayAllocator>::free,
};
// Push a tensor, assuming arr is a numpy array of appropriate type
template <class T>
void pushTensor(lua_State* L, const PyObjectHandle& oh) {
  auto arr = reinterpret_cast<PyArrayObject*>(oh.get());
  auto storage = thpp::Storage<T>::wrapWithAllocator(
      static_cast<T*>(PyArray_DATA(arr)),
      PyArray_NBYTES(arr) / sizeof(T),
      &numpyArrayTHAllocator,
      new NumpyArrayAllocator(oh));

  // Numpy and Torch disagree on empty tensors. In Torch, an empty
  // tensor is a tensor with zero dimensions. In Numpy, an empty tensor
  // keeps its shape, but has 0 as the size of one of the dimensions.
  // So we'll convert all Numpy tensors of 0 elements to empty Torch tensors.
  // Also see LuaToPythonConverter::convertTensor in LuaToPython.cpp.
  if (PyArray_SIZE(arr) != 0) {
    auto ndims = PyArray_NDIM(arr);
    thpp::LongStorage sizes(ndims, 0L);
    for (size_t i = 0; i < ndims; ++i) {
      sizes[i] = PyArray_DIM(arr, i);
    }

    thpp::LongStorage strides(ndims, 0L);
    for (size_t i = 0; i < ndims; ++i) {
      long s = PyArray_STRIDE(arr, i);
      DCHECK_EQ(s % sizeof(T), 0);  // must be aligned
      strides[i] = s / sizeof(T);   // numpy uses bytes, torch uses elements
    }

    thpp::Tensor<T> tensor(std::move(storage), 0, std::move(sizes),
                           std::move(strides));
    luaPushTensor(L, std::move(tensor));
  } else {
    luaPushTensor(L, thpp::Tensor<T>());
  }
}

}  // namespace

int PythonToLuaConverter::convert(lua_State* L, const PyObjectHandle& oh) {
  // We need one slot on the stack for every nesting level of Python
  // objects, plus a few slots at the top to work with. 400 sounds reasonable.
  // The default is 20, which is definitely not.
  static constexpr int kStackSize = 400;
  luaL_checkstack(L, kStackSize, "PythonToLua: out of stack memory");

  // Create list of converted objects
  lua_newtable(L);
  convertedIdx_ = lua_gettop(L);
  convertedCount_ = 0;

  return doConvert(L, oh);
}

int PythonToLuaConverter::doConvert(lua_State* L, const PyObjectHandle& oh) {
  {
    auto pos = converted_.find(oh);
    if (pos != converted_.end()) {
      // Copy to the top of the stack
      lua_rawgeti(L, convertedIdx_, pos->second);
      return 1;
    }
  }

  PyObject* obj = oh.get();  // for convenience, it never changes

  bool recorded = false;
  auto record = [&] () {
    DCHECK(!recorded);
    auto& p = converted_[oh];
    DCHECK_EQ(p, 0);
    p = ++convertedCount_;
    lua_pushvalue(L, -1);  // make a copy for lua_rawseti
    lua_rawseti(L, convertedIdx_, p);
    recorded = true;
  };

  // We use the concrete interface as much as possible, as numpy makes a
  // mess of the abstract interface (arrays pretend to implement the number
  // protocol and fail at runtime if you try to convert them to float...)

  if (obj == Py_None) {                          // None
    switch(noneMode_) {
    case NONE_AS_LUA_NIL:
      lua_pushnil(L);
      break;
    case NONE_AS_LUAPY_NONE:
      pushOpaqueRef(L, PyObjectHandle(PyObjectHandle::INCREF, Py_None));
      break;
    }
  } else if (PyBool_Check(obj)) {                // bool
    lua_pushboolean(L, obj != Py_False);
  } else if (PyInt_Check(obj)) {                 // int
    // Python int === C long
    lua_pushinteger(L, PyInt_AS_LONG(obj));
  } else if (PyLong_Check(obj)) {                // long
    // Python long is arbitrarily long
    long val = PyLong_AsLong(obj);
    if (val == -1 && PyErr_Occurred()) {
      raisePythonError(L, "convert Python long to C long (out of range?)");
    }
    lua_pushinteger(L, val);
  } else if (PyFloat_Check(obj)) {               // float
    // Python float === C double
    lua_pushnumber(L, PyFloat_AS_DOUBLE(obj));
  } else if (PyArray_CheckScalar(obj)) {         // numpy scalar (float32, etc)
    double val = 0;
    // Cast to double
    PyArray_CastScalarToCtype(obj, &val, PyArray_DescrFromType(NPY_DOUBLE));
    lua_pushnumber(L, val);
  } else if (PyBytes_Check(obj)) {
    char* data = PyString_AS_STRING(obj);
    Py_ssize_t len = PyString_GET_SIZE(obj);
    lua_pushlstring(L, data, len);
  } else if (PyUnicode_Check(obj)) {
    PyObjectHandle str(PyUnicode_AsUTF8String(obj));
    checkPythonError(str, L, "convert unicode");
    char* data = PyString_AS_STRING(str.get());
    Py_ssize_t len = PyString_GET_SIZE(str.get());
    lua_pushlstring(L, data, len);
  } else if (PyDict_Check(obj)) {                // dict
    lua_newtable(L);
    // In order to detect cycles properly, we need to record the reference
    // *before* we convert its children.
    record();

    Py_ssize_t pos = 0;
    PyObject* keyObj = nullptr;
    PyObject* valueObj = nullptr;

    // key, value are borrowed
    while (PyDict_Next(obj, &pos, &keyObj, &valueObj)) {
      checkPythonError(keyObj, L, "retrieve dictionary key");
      checkPythonError(valueObj, L, "retrieve dictionary value");
      PyObjectHandle key(PyObjectHandle::INCREF, keyObj);
      PyObjectHandle value(PyObjectHandle::INCREF, valueObj);

      doConvert(L, key);
      doConvert(L, value);

      lua_rawset(L, -3);
    }
  } else if (PyList_Check(obj) || PyTuple_Check(obj)) {  // list or tuple
    // PySequence_Fast returns the same object if it's a list or tuple,
    // which it is.
    lua_newtable(L);
    // In order to detect cycles properly, we need to record the reference
    // *before* we convert its children.
    record();

    // We're calling PySequence_Fast_GET_SIZE every time through the loop, in
    // case some code underneath changes the list size
    // (PySequence_Fast_GET_ITEM assumes that the index is within bounds)
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(obj); ++i) {
      PyObjectHandle item(PyObjectHandle::INCREF,
                          PySequence_Fast_GET_ITEM(obj, i));  // borrowed
      checkPythonError(item, L, "retrieve list item");

      doConvert(L, item);
      lua_rawseti(L, -2, i + 1);  // note 1-based indexing in Lua
    }
  } else if (PyArray_Check(obj)) {                // numpy.ndarray
    PyObjectHandle arr(PyArray_FromArray(
        reinterpret_cast<PyArrayObject*>(obj),
        nullptr,
        NPY_ARRAY_BEHAVED));  // properly aligned and writable
    checkPythonError(arr, L, "get well-behaved numpy array");
    PyArrayObject* arrObj = reinterpret_cast<PyArrayObject*>(arr.get());

#define NDARRAY_TO_TENSOR(TYPE, NUMPY_TYPE) \
    case NUMPY_TYPE: \
      pushTensor<TYPE>(L, oh); \
      break

    switch (PyArray_TYPE(arrObj)) {
    NDARRAY_TO_TENSOR(double, NPY_DOUBLE);
    NDARRAY_TO_TENSOR(float, NPY_FLOAT);
    NDARRAY_TO_TENSOR(int, NPY_INT32);
    NDARRAY_TO_TENSOR(long, NPY_INT64);
    NDARRAY_TO_TENSOR(unsigned char, NPY_UINT8);
    default:
      luaL_error(L, "Invalid numpy data type %d", PyArray_TYPE(arrObj));
    }
  } else {
    luaL_error(L, "Unsupported Python object");
  }
  if (!recorded) {
    record();
  }
  return 1;
}

int initPythonToLua(lua_State* L) {
  initNumpyArrayAllocator(L);
  initNumpy(L);
  return 0;
}

}}  // namespaces
