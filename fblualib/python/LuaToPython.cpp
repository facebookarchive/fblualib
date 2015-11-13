/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "LuaToPython.h"
#include "Ref.h"
#include "Storage.h"

namespace fblualib {
namespace python {

// Record objects that we've already converted, so identical objects convert
// to identical objects (and also, so objects with cycles terminate)
void LuaToPythonConverter::record(lua_State* L, int index,
                                  const PyObjectHandle& obj) {
  const void* luaPtr = lua_topointer(L, index);
  if (!luaPtr) {
    return;
  }
  auto& p = converted_[luaPtr];
  DCHECK(!p);
  p = obj;
}

PyObjectHandle LuaToPythonConverter::checkRecorded(lua_State* L, int index) {
  // Check if already converted; if so, return a reference to the same object
  const void* ptr = lua_topointer(L, index);
  if (ptr == 0) {
    return nullptr;
  }

  auto pos = converted_.find(ptr);
  if (pos != converted_.end()) {
    return pos->second;
  }

  return nullptr;
}

PyObjectHandle LuaToPythonConverter::convert(lua_State* L, int index,
                                        unsigned flags) {
  fixIndex(L, index);

  PyObjectHandle obj = checkRecorded(L, index);
  if (obj) {
    return obj;
  }

  bool recorded = false;
  bool isInvalid = false;

  int type = lua_type(L, index);
  switch (type) {
  case LUA_TNIL:            // nil -> None
    obj.reset(PyObjectHandle::INCREF, Py_None);
    break;

  case LUA_TNUMBER:         // Lua number -> Python float
    {
      double val = lua_tonumber(L, index);
      if (flags & INTEGRAL_NUMBERS) {
        long lval = folly::to<long>(val);
        obj.reset(PyInt_FromLong(lval));
      } else {
        obj.reset(PyFloat_FromDouble(val));
      }
    }
    break;

  case LUA_TBOOLEAN:        // Lua boolean -> Python bool
    obj.reset(PyBool_FromLong(lua_toboolean(L, index)));
    break;

  case LUA_TSTRING:         // Lua string -> Python bytes
                            // (non-unicode byte string)
    {
      size_t len;
      const char* str = lua_tolstring(L, index, &len);
      obj.reset(PyBytes_FromStringAndSize(str, len));
    }
    break;

  case LUA_TTABLE:          // Lua table -> Python list or dict
    if (flags & BUILTIN_TYPES_ONLY) {
      luaL_error(L, "'%s' is not a builtin type", lua_typename(L, type));
    }
    obj = convertFromTable(L, index);
    recorded = true;
    break;

  case LUA_TUSERDATA:       // Lua userdata -> special cased
    // Opaque ref
    {
      if (auto ref = getOpaqueRef(L, index)) {
        obj = ref->obj;
        break;
      }
    }
    if (flags & BUILTIN_TYPES_ONLY) {
      luaL_error(L, "'%s' is not a builtin type", lua_typename(L, type));
    }
    // Torch tensor -> numpy.ndarray
#define TENSOR_TO_NDARRAY(TYPE, NUMPY_TYPE) \
    { \
      auto tensor = luaGetTensor<TYPE>(L, index); \
      if (tensor) { \
        obj = convertTensor<TYPE>(L, **tensor, NUMPY_TYPE); \
        break; \
      } \
    }
    TENSOR_TO_NDARRAY(double, NPY_DOUBLE);
    TENSOR_TO_NDARRAY(float, NPY_FLOAT);
    TENSOR_TO_NDARRAY(int32_t, NPY_INT32);
    TENSOR_TO_NDARRAY(int64_t, NPY_INT64);
    TENSOR_TO_NDARRAY(uint8_t, NPY_UINT8);
#undef TENSOR_TO_NDARRAY
    isInvalid = true;
    break;

  case LUA_TLIGHTUSERDATA:  // Lightuserdata, unsupported
  case LUA_TFUNCTION:       // Lua function, unsupported
  case LUA_TTHREAD:         // Lua coroutine, unsupported
    isInvalid = true;
    break;

  case LUA_TNONE:           // Invalid stack index
    luaL_error(L, "Invalid stack index %d", index);
    break;

  default:                  // WTF type is this?
    if (!(flags & IGNORE_INVALID_TYPES)) {
      luaL_error(L, "Cannot convert unknown type %d to Python type", type);
    }
    if (flags & BUILTIN_TYPES_ONLY) {
      luaL_error(L, "Unknown type %d is not a builtin type", type);
    }
    return nullptr;
  }

  if (isInvalid) {
    if (!(flags & IGNORE_INVALID_TYPES)) {
      luaL_error(L, "Cannot convert '%s' to Python type",
                 lua_typename(L, type));
    }
    if (flags & BUILTIN_TYPES_ONLY) {
      luaL_error(L, "'%s' is not a builtin type", lua_typename(L, type));
    }
    return nullptr;
  }

  checkPythonError(obj, L, "lua->python conversion, type {}", type);

  if (!recorded) {
    record(L, index, obj);
  }

  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToFloat(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    PyObjectHandle obj(PyNumber_Float(ref->obj.get()));
    checkPythonError(obj, L, "convertToFloat(ref)");
    return obj;
  }

  int type = lua_type(L, index);
  if (type != LUA_TNUMBER) {
    luaL_error(L, "Invalid type for convertToFloat: %d", type);
  }
  double val = lua_tonumber(L, index);
  PyObjectHandle obj(PyFloat_FromDouble(val));
  checkPythonError(obj, L, "convertToFloat");
  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToInt(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    PyObjectHandle obj(PyNumber_Int(ref->obj.get()));
    checkPythonError(obj, L, "convertToInt(ref)");
    return obj;
  }
  int type = lua_type(L, index);
  if (type != LUA_TNUMBER) {
    luaL_error(L, "Invalid type for convertToInt: %d", type);
  }
  double val = lua_tonumber(L, index);
  long lval = folly::to<long>(val);
  PyObjectHandle obj(PyInt_FromLong(lval));
  checkPythonError(obj, L, "convertToInt");
  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToLong(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    PyObjectHandle obj(PyNumber_Long(ref->obj.get()));
    checkPythonError(obj, L, "convertToLong(ref)");
    return obj;
  }

  int type = lua_type(L, index);
  if (type != LUA_TNUMBER) {
    luaL_error(L, "Invalid type for convertToLong: %d", type);
  }
  double val = lua_tonumber(L, index);
  long lval = folly::to<long>(val);
  PyObjectHandle obj(PyLong_FromLong(lval));
  checkPythonError(obj, L, "convertToLong");
  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToBytes(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // Must convert to a Python bytes object.
    if (PyBytes_Check(ref->obj.get())) {
      return ref->obj;
    } else if (PyUnicode_Check(ref->obj.get())) {
      PyObjectHandle obj(PyUnicode_AsUTF8String(ref->obj.get()));
      checkPythonError(obj, L, "convertToBytes(unicode)");
      return obj;
    } else {
      luaL_error(L, "neither bytes nor unicode");
    }
  }

  int type = lua_type(L, index);
  if (type != LUA_TSTRING) {
    luaL_error(L, "Invalid type for convertToBytes: %d", type);
  }

  size_t len;
  const char* str = lua_tolstring(L, index, &len);
  PyObjectHandle obj(PyBytes_FromStringAndSize(str, len));
  checkPythonError(obj, L, "convertToBytes");
  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToUnicode(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // Must convert to a Python unicode object.
    if (PyBytes_Check(ref->obj.get())) {
      char* data = PyBytes_AS_STRING(ref->obj.get());
      Py_ssize_t len = PyBytes_GET_SIZE(ref->obj.get());
      PyObjectHandle obj(PyUnicode_DecodeUTF8(data, len, "strict"));
      checkPythonError(obj, L, "convertToUnicode(bytes)");
      return obj;
    } else if (PyUnicode_Check(ref->obj.get())) {
      return ref->obj;
    } else {
      luaL_error(L, "neither bytes nor unicode");
    }
  }

  int type = lua_type(L, index);
  if (type != LUA_TSTRING) {
    luaL_error(L, "Invalid type for convertToUnicode: %d", type);
  }

  size_t len;
  const char* str = lua_tolstring(L, index, &len);
  PyObjectHandle obj(PyUnicode_DecodeUTF8(str, len, "strict"));
  checkPythonError(obj, L, "convertToUnicdoe");
  return obj;
}

PyObjectHandle LuaToPythonConverter::convertToTuple(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // Must convert to a Python tuple.
    PyObjectHandle tup(PySequence_Tuple(ref->obj.get()));
    checkPythonError(tup, L, "cannot convert to tuple");
    return tup;
  }

  return convertTupleFromTable(L, index, false);
}

PyObjectHandle LuaToPythonConverter::convertToList(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // Must convert to a Python list
    PyObjectHandle list(PySequence_List(ref->obj.get()));
    checkPythonError(list, L, "cannot convert to list");
    return list;
  }

  return convertListFromTable(L, index, false);
}

PyObjectHandle LuaToPythonConverter::convertToFastSequence(lua_State* L,
                                                           int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // Must convert to a Python list or tuple
    PyObjectHandle seq(PySequence_Fast(ref->obj.get(), ""));
    checkPythonError(seq, L, "cannot convert to fast sequence");
    return seq;
  }

  return convertTupleFromTable(L, index, false);
}

PyObjectHandle LuaToPythonConverter::convertToDict(lua_State* L, int index) {
  fixIndex(L, index);

  if (auto ref = getOpaqueRef(L, index)) {
    // There's no simple way to convert arbitrary mappings to dict.
    // Let's just bail out if it isn't a dict.
    checkPythonError(PyDict_Check(ref->obj.get()), L, "not a dict");
    return ref->obj;
  }

  return convertDictFromTable(L, index, false);
}

// Return true iff the table at the given index has the given numeric key
bool listFieldExists(lua_State* L, int index, int field) {
  lua_rawgeti(L, index, field);
  bool r = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return r;
}

PyObjectHandle LuaToPythonConverter::convertFromTable(lua_State* L, int index) {
  // Check if it's a list
  // Note 0- vs 1- based indexing!
  // Heuristic: if x[1] exists and x[0] and x[-1] don't, it's a list, otherwise
  // it's a dict.
  bool isList = (
      listFieldExists(L, index, 1) &&
      !listFieldExists(L, index, 0) &&
      !listFieldExists(L, index, -1));

  return
    isList ? convertListFromTable(L, index) : convertDictFromTable(L, index);
}

// In order to detect cycles properly, we need to record the reference
// *before* we convert its children.
#define DEFINE_SEQUENCE_CONVERT_FUNCTION(TYPE) \
  PyObjectHandle LuaToPythonConverter::convert ## TYPE ## FromTable( \
      lua_State* L, int index, bool rec) { \
    size_t len = luaListSizeChecked(L, index); \
    PyObjectHandle obj(Py ## TYPE ## _New(len)); \
    checkPythonError(obj, L, "convert" #TYPE "FromTable"); \
    if (rec) { \
      record(L, index, obj); \
    } \
\
    for (size_t i = 0; i < len; ++i) { \
      lua_rawgeti(L, index, i + 1); \
      Py ## TYPE ##_SET_ITEM(obj.get(), i, convert(L, -1).release()); \
      lua_pop(L, 1); \
    } \
\
    return obj; \
  }

DEFINE_SEQUENCE_CONVERT_FUNCTION(List)
DEFINE_SEQUENCE_CONVERT_FUNCTION(Tuple)

#undef DEFINE_SEQUENCE_CONVERT_FUNCTION

PyObjectHandle LuaToPythonConverter::convertDictFromTable(
    lua_State* L, int index, bool rec) {
  if (lua_type(L, index) != LUA_TTABLE) {
    luaL_error(L, "must be table");
  }
  PyObjectHandle obj(PyDict_New());
  checkPythonError(obj, L, "convertDictFromTable");
  if (rec) {
    // In order to detect cycles properly, we need to record the reference
    // *before* we convert its children.
    record(L, index, obj);
  }

  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    // key at index -2, value at index -1
    // Python can't use arbitrary types as dict keys; only allow numbers
    // and strings. Also, if you use float keys, you're insane.
    auto key = convert(L, -2, BUILTIN_TYPES_ONLY | INTEGRAL_NUMBERS);

    // Don't try to serialize member functions for classes...
    auto value = convert(L, -1, IGNORE_INVALID_TYPES);
    if (value) {
      PyDict_SetItem(obj.get(), key.get(), value.get());
    }
    lua_pop(L, 1);  // keep key for next iteration
  }

  return obj;
}

template <class T>
PyObjectHandle LuaToPythonConverter::convertTensor(lua_State* L,
                                                   thpp::Tensor<T>& tensor,
                                                   int numpyType) {
  npy_intp zero = 0;
  int ndims;
  std::unique_ptr<npy_intp[]> dims;
  npy_intp* dimsPtr;
  std::unique_ptr<npy_intp[]> strides;

  // Numpy and Torch disagree on empty tensors. In Torch, an empty tensor
  // is a tensor with zero dimensions. In Numpy, a tensor with zero dimensions
  // is a scalar (with one element). So we'll convert an empty Torch tensor
  // to a 1d Numpy tensor of shape [0]. Also see pushTensor in PythonToLua.cpp.
  if (tensor.ndims() != 0) {
    ndims = tensor.ndims();
    auto tsizes = tensor.sizes();
    DCHECK_EQ(tsizes.size(), ndims);

    dims.reset(new npy_intp[ndims]);
    dimsPtr = dims.get();
    std::copy(tsizes.begin(), tsizes.end(), dims.get());

    if (!tensor.isContiguous()) {
      auto tstrides = tensor.strides();
      DCHECK_EQ(tstrides.size(), ndims);

      strides.reset(new npy_intp[ndims]);

      // Numpy strides use bytes; Torch strides use element counts.
      for (int i = 0; i < ndims; ++i) {
        strides[i] = tstrides[i] * sizeof(T);
      }
    }
  } else {
    ndims = 1;
    dimsPtr = &zero;
  }

  PyObjectHandle obj(PyArray_New(
      &PyArray_Type, ndims, dimsPtr, numpyType,
      strides.get(), tensor.data(), 0,
      NPY_ARRAY_ALIGNED, nullptr));
  checkPythonError(obj, L, "create numpy.ndarray of type {}", numpyType);

  // Create a PythonStorage object to hold the reference count.
  // PyArray_SetBaseObject steals the reference to the base object.
  int r = PyArray_SetBaseObject(reinterpret_cast<PyArrayObject*>(obj.get()),
                                PythonStorage<T>::allocate(
                                    L, tensor.storage()).release());
  checkPythonError(r != -1, L, "SetBaseObject on numpy.ndarray");
  return obj;
}

int initLuaToPython(lua_State* L) {
  initNumpy(L);
  initStorage(L);
  return 0;
}

}}  // namespaces
