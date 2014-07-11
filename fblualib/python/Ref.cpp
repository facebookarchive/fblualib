/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "Ref.h"

#include "LuaToPython.h"

namespace fblualib {
namespace python {

namespace {
const char* kOpaqueRefType = "fblualib.python.opaque_ref";

// Lightuserdata pointers (&argsPlaceholder, &kwargsPlaceholder) to identify
// the "args" and "kwargs" objects.
char argsPlaceholder;
char kwargsPlaceholder;
}  // namespace

OpaqueRef* checkOpaqueRef(lua_State* L, int index) {
  auto ref = static_cast<OpaqueRef*>(
      luaL_checkudata(L, index, kOpaqueRefType));
  DCHECK(ref && ref->obj);
  return ref;
}

OpaqueRef* getOpaqueRef(lua_State* L, int index) {
  if (lua_type(L, index) != LUA_TUSERDATA) {
    return nullptr;
  }
  int r = lua_getmetatable(L, index);
  if (r == 0) {
    return nullptr;  // no metatable => not one of our objects
  }
  luaL_getmetatable(L, kOpaqueRefType);
  bool isOpaqueRef = lua_equal(L, -1, -2);
  lua_pop(L, 2);
  if (!isOpaqueRef) {
    return nullptr;
  }
  auto ref = static_cast<OpaqueRef*>(lua_touserdata(L, index));
  DCHECK(ref && ref->obj);
  return ref;
}

int pushOpaqueRef(lua_State* L, PyObjectHandle obj) {
  auto ref = new (lua_newuserdata(L, sizeof(OpaqueRef)))
    OpaqueRef(std::move(obj));
  luaL_getmetatable(L, kOpaqueRefType);
  lua_setmetatable(L, -2);
  return 1;
}

namespace {

// Retrieve a Python object suitable to use as a key.
// The second element of the pair is true if we should try attribute access
// first.
std::pair<PyObjectHandle, bool> getKey(lua_State* L, int index) {
  int type = lua_type(L, index);
  std::pair<PyObjectHandle, bool> p(nullptr, false);
  switch (type) {
  case LUA_TSTRING:
    {
      size_t len;
      const char* key = lua_tolstring(L, index, &len);
      p.first.reset(PyBytes_FromStringAndSize(key, len));
      p.second = true;
    }
    break;
  case LUA_TNUMBER:
    {
      double val = lua_tonumber(L, index);
      long lval = folly::to<long>(val);
      p.first.reset(PyInt_FromLong(lval));
      p.second = false;
    }
    break;
  case LUA_TUSERDATA:
    {
      if (auto ref = getOpaqueRef(L, index)) {
        p.first = ref->obj;
        p.second = false;
        break;
      }
    }
    // fallthrough
  default:
    luaL_error(L, "Invalid key type %d", type);
  }
  checkPythonError(p.first, L, "create key");
  return p;
}

int refIndex(lua_State* L) {
  auto ref = checkOpaqueRef(L, 1);
  PythonGuard g;
  auto p = getKey(L, 2);

  PyObjectHandle res = nullptr;
  if (p.second) {  // is string
    // Unfortunately, we can't determine whether o[x] is attribute access
    // or item access, as Lua doesn't distinguish. We'll try attributes first.
    // This means that d['get'] will not work as intended if d is a dict.
    if (PyObject_HasAttr(ref->obj.get(), p.first.get())) {
      res.reset(PyObject_GetAttr(ref->obj.get(), p.first.get()));
      checkPythonError(res, L, "opaque ref: attribute lookup");
    }
  }
  if (!res) {
    res.reset(PyObject_GetItem(ref->obj.get(), p.first.get()));
    checkPythonError(res, L, "opaque ref: item lookup");
  }

  pushOpaqueRef(L, std::move(res));
  return 1;
}

int refNewIndex(lua_State* L) {
  auto ref = checkOpaqueRef(L, 1);
  PythonGuard g;
  auto p = getKey(L, 2);

  LuaToPythonConverter converter;
  auto value = converter.convert(L, 3);

  bool found = false;
  int res = -1;
  if (p.second) {  // is string
    if (PyObject_HasAttr(ref->obj.get(), p.first.get())) {
      res = PyObject_SetAttr(ref->obj.get(), p.first.get(), value.get());
      checkPythonError(res != -1, L, "opaque ref: attribute update");
    }
  }
  if (res == -1) {
    res = PyObject_SetItem(ref->obj.get(), p.first.get(), value.get());
    checkPythonError(res != -1, L, "opaque ref: item update");
  }

  return 0;
}

int refCall(lua_State* L) {
  auto ref = checkOpaqueRef(L, 1);
  PythonGuard g;
  constexpr int firstArg = 2;

  size_t nargs = 0;
  size_t nreg = 0;
  int top = lua_gettop(L);
  int varargsIdx = 0;
  int kwargsIdx = 0;
  PyObjectHandle varargs = nullptr;
  PyObjectHandle kwargs = nullptr;

  LuaToPythonConverter converter;
  for (int i = firstArg; i <= top; ++i) {
    const void* ptr = lua_topointer(L, i);
    if (ptr == &argsPlaceholder) {
      if (varargsIdx) {
        luaL_error(L, "'args' specified twice");
      }
      if (kwargsIdx) {
        luaL_error(L, "'args' must come before kwargs");
      }
      if (i == top) {
        luaL_error(L, "'args' missing argument");
      }
      varargsIdx = i;
      ++i;
      varargs = converter.convertToFastSequence(L, i);
      nargs += PySequence_Fast_GET_SIZE(varargs.get());
    } else if (ptr == &kwargsPlaceholder) {
      if (kwargsIdx) {
        luaL_error(L, "'kwargs' specified twice");
      }
      if (i == top) {
        luaL_error(L, "'kwargs' missing argument");
      }
      kwargsIdx = i;
      ++i;
      kwargs = converter.convertToDict(L, i);
    } else {
      if (varargsIdx || kwargsIdx) {
        luaL_error(L, "regular arguments specified after args / kwargs");
      }
      ++nargs;
      ++nreg;
    }
  }

  PyObjectHandle args = nullptr;
  size_t n = 0;
  if (nreg == 0 && varargs && PyTuple_Check(varargs.get())) {
    args = varargs;
    n = PyTuple_GET_SIZE(varargs.get());
  } else {
    args.reset(PyTuple_New(nargs));
    checkPythonError(args, L, "opaque ref: call: create arg tuple");

    for (int i = 1; i <= nreg; ++i, ++n) {
      auto arg = converter.convert(L, firstArg + i - 1);
      checkPythonError(arg, L, "opaque ref: call: arg {}", i);
      PyTuple_SET_ITEM(args.get(), n, arg.release());  // steals arg
    }

    if (varargs) {
      for (int i = 1; i <= PySequence_Fast_GET_SIZE(varargs.get()); ++i, ++n) {
        PyObjectHandle arg(
            PyObjectHandle::INCREF,
            PySequence_Fast_GET_ITEM(varargs.get(), i - 1));  // borrowed
        checkPythonError(arg, L, "opaque ref: varg {}", i);
        PyTuple_SET_ITEM(args.get(), n, arg.release());  // steals arg
      }
    }
  }
  DCHECK_EQ(n, nargs);

  PyObjectHandle res(PyObject_Call(ref->obj.get(), args.get(), kwargs.get()));
  checkPythonError(res, L, "opaque ref: call");

  pushOpaqueRef(L, std::move(res));
  return 1;
}

int refStr(lua_State* L) {
  auto ref = checkOpaqueRef(L, 1);
  PythonGuard g;
  PyObjectHandle str(PyObject_Str(ref->obj.get()));
  checkPythonError(str, L, "opaque ref: str");
  char* data;
  Py_ssize_t len;
  int r = PyString_AsStringAndSize(str.get(), &data, &len);
  checkPythonError(r != -1, L, "opaque ref: str: as_string");
  lua_pushlstring(L, data, len);
  return 1;
}

int refNegative(lua_State* L) {
  PythonGuard g;
  LuaToPythonConverter converter;
  auto obj = converter.convert(L, 1);
  PyObjectHandle res(PyNumber_Negative(obj.get()));
  checkPythonError(res, L, "ref Negative");
  pushOpaqueRef(L, std::move(res));
  return 1;
}

int refLen(lua_State* L) {
  PythonGuard g;
  LuaToPythonConverter converter;
  auto obj = converter.convert(L, 1);
  Py_ssize_t len = PyObject_Length(obj.get());
  checkPythonError(len != -1, L, "ref Len");
  lua_pushinteger(L, len);
  return 1;
}

#define REF_BINARY_OP1(OP, FN) \
  int ref##OP(lua_State* L) { \
    PythonGuard g; \
    LuaToPythonConverter converter; \
    auto left = converter.convert(L, 1); \
    auto right = converter.convert(L, 2); \
    PyObjectHandle res(FN(left.get(), right.get())); \
    checkPythonError(res, L, "ref " #OP); \
    pushOpaqueRef(L, std::move(res)); \
    return 1; \
  }

#define REF_BINARY_OP(OP) REF_BINARY_OP1(OP, PyNumber_##OP)

REF_BINARY_OP(Add)
REF_BINARY_OP(Subtract)
REF_BINARY_OP(Multiply)
REF_BINARY_OP(Divide)
REF_BINARY_OP(Remainder)

inline PyObject* doPower(PyObject* left, PyObject* right) {
  return PyNumber_Power(left, right, Py_None);
}

REF_BINARY_OP1(Power, doPower)

#undef REF_BINARY_OP
#undef REF_BINARY_OP1

#define REF_COMPARISON_OP(OP) \
  int ref##OP(lua_State* L) { \
    PythonGuard g; \
    LuaToPythonConverter converter; \
    auto left = converter.convert(L, 1); \
    auto right = converter.convert(L, 2); \
    int res = PyObject_RichCompareBool(left.get(), right.get(), Py_##OP); \
    checkPythonError(res != -1, L, "ref " #OP); \
    lua_pushboolean(L, res); \
    return 1; \
  }

REF_COMPARISON_OP(EQ)
REF_COMPARISON_OP(LT)
REF_COMPARISON_OP(LE)

#undef REF_COMPARISON_OP

int refDelete(lua_State* L) {
  auto ref = checkOpaqueRef(L, 1);
  PythonGuard g;
  ref->~OpaqueRef();
  return 0;
}

const struct luaL_reg kOpaqueRefMeta[] = {
  {"__index", refIndex},
  {"__newindex", refNewIndex},
  {"__unm", refNegative},
  {"__add", refAdd},
  {"__sub", refSubtract},
  {"__mul", refMultiply},
  {"__div", refDivide},
  {"__mod", refRemainder},
  {"__pow", refPower},
  {"__concat", refAdd},  // Python doesn't distinguish between '..' and '+'
  {"__call", refCall},
  {"__tostring", refStr},
  {"__len", refLen},
  {"__eq", refEQ},
  {"__lt", refLT},
  {"__le", refLE},
  {"__gc", refDelete},
  {nullptr, nullptr},
};

}  // namespace

int initRef(lua_State* L) {
  PythonGuard g;
  if (luaL_newmetatable(L, kOpaqueRefType)) {
    // not already registered
    luaL_register(L, nullptr, kOpaqueRefMeta);
    lua_pop(L, 1);
  }
  lua_pushlightuserdata(L, &argsPlaceholder);
  lua_setfield(L, -2, "args");
  lua_pushlightuserdata(L, &kwargsPlaceholder);
  lua_setfield(L, -2, "kwargs");
  pushOpaqueRef(L, PyObjectHandle(PyObjectHandle::INCREF, Py_None));
  lua_setfield(L, -2, "None");
  return 0;
}

}}  // namespaces
