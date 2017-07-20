/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <Python.h>
#include <glog/logging.h>
#include <dlfcn.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include <mutex>

#include "LuaToPython.h"
#include "PythonToLua.h"
#include "Ref.h"
#include "Utils.h"

using namespace fblualib;
using namespace fblualib::python;

namespace {

// Lua stack: code, locals, ignore unknown
PyObjectHandle doExec(lua_State* L, int startSymbol) {
  const char* code = lua_tostring(L, 1);
  if (!code) {
    luaL_error(L, "Python code must be string");
  }

  PyObject* mainModule = PyImport_AddModule("__main__");  // borrowed
  checkPythonError(mainModule, L, "add main module");

  PyObjectHandle mainDict(
      PyObjectHandle::INCREF,
      PyModule_GetDict(mainModule));  // borrowed
  checkPythonError(mainDict, L, "get main module dict");

  PyObjectHandle locals;

  int type = lua_type(L, 2);
  if (type == LUA_TNONE || type == LUA_TNIL) {
    // No locals; run at module namespace (__main__ module)
    locals = mainDict;
  } else {
    unsigned flags = 0;
    if (lua_toboolean(L, 3)) {
      flags |= LuaToPythonConverter::IGNORE_INVALID_TYPES;
    }
    LuaToPythonConverter converter;
    locals = converter.convert(L, 2, flags);
  }

  PyObjectHandle ret(PyRun_String(code, startSymbol,
                                  mainDict.get(), locals.get()));
  checkPythonError(ret, L, "execute Python code");
  return ret;
}

int execPython(lua_State* L) {
  PythonGuard g;
  doExec(L, Py_file_input);
  return 0;
}

int evalInner(lua_State* L, PythonToLuaConverter::NoneMode noneMode) {
  PythonGuard g;
  PyObjectHandle ret;
  auto ref = getOpaqueRef(L, 1);
  PythonToLuaConverter converter(noneMode);
  return converter.convert(L, ref ? ref->obj : doExec(L, Py_eval_input));
}

int evalPython(lua_State* L) {
  return evalInner(L, PythonToLuaConverter::NONE_AS_LUA_NIL);
}

int evalNonePython(lua_State* L) {
  return evalInner(L, PythonToLuaConverter::NONE_AS_LUAPY_NONE);
}

int refEvalPython(lua_State* L) {
  PythonGuard g;
  if (getOpaqueRef(L, 1)) {
    lua_pushvalue(L, 1);
  } else {
    pushOpaqueRef(L, doExec(L, Py_eval_input));
  }
  return 1;
}

int doConvert(lua_State* L,
              PyObjectHandle (LuaToPythonConverter::*method)(lua_State*, int)) {
  PythonGuard g;
  LuaToPythonConverter converter;
  pushOpaqueRef(L, (converter.*method)(L, 1));
  return 1;
}

int getRef(lua_State* L) {
  return doConvert(L, &LuaToPythonConverter::convertDefault);
}

#define DEFINE_GET(N) \
int get ## N(lua_State* L) { \
  return doConvert(L, &LuaToPythonConverter::convertTo ## N); \
}

DEFINE_GET(Float)
DEFINE_GET(Int)
DEFINE_GET(Long)
DEFINE_GET(Bytes)
DEFINE_GET(Unicode)
DEFINE_GET(Tuple)
DEFINE_GET(List)
DEFINE_GET(Dict)

#undef DEFINE_GET

int checkNoRefs(lua_State* /*L*/) {
  debugCheckNoRefs();
  return 0;
}

namespace {
PyObjectHandle defaultFromList(lua_State* L) {
  PyObjectHandle fromlist(PyList_New(1));
  checkPythonError(fromlist, L, "create default import list");
  PyObjectHandle all(PyString_FromString("*"));
  checkPythonError(all, L, "default import string");
  PyList_SET_ITEM(fromlist.get(), 0, all.release());  // steals ref
  return fromlist;
}
}  // namespace

int getModule(lua_State* L) {
  PythonGuard g;
  const char* name = lua_tostring(L, 1);
  if (!name) {
    luaL_error(L, "Module name must be string");
  }

  PyObjectHandle fromList;
  bool explicitList = false;
  if (lua_gettop(L) >= 2) {
    LuaToPythonConverter converter;
    fromList = converter.convertToList(L, 2);
    if (PyList_GET_SIZE(fromList.get()) != 0) {
      explicitList = true;
    }
  }
  if (!explicitList) {
    fromList = defaultFromList(L);
  }

  PyObjectHandle module(PyImport_ImportModuleEx(
          const_cast<char*>(name), nullptr, nullptr, fromList.get()));
  checkPythonError(module, L, "import");
  pushOpaqueRef(L, std::move(module));

  return 1;
}

const struct luaL_reg pythonFuncs[] = {
  {"exec", execPython},
  {"eval", evalPython},
  {"eval_none", evalNonePython},
  {"reval", refEvalPython},
  {"ref", getRef},
  {"float", getFloat},
  {"int", getInt},
  {"long", getLong},
  {"bytes", getBytes},
  {"str", getBytes},  // Python 2.x
  {"unicode", getUnicode},
  {"list", getList},
  {"tuple", getTuple},
  {"dict", getDict},
  {"import", getModule},
  {"_check_no_refs", checkNoRefs},
  {nullptr, nullptr},
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L);

namespace {

// This module loads libpython.so, which needs to be made available for
// resolution to numpy's internal modules (multiarray.so does not have a
// DT_NEEDED dependency on libpython.so). The trouble is that LuaJIT loads C
// extensions with RTLD_LOCAL and there's no easy way to override that, so
// symbols from this module (and its dependencies) are not available to
// numpy's internal modules.
//
// We fix this by reloading the current module with RTLD_GLOBAL. Note the
// RTLD_NOLOAD flag; we'll fail if the library isn't already loaded (which
// would mean we're doing something stupid).
void reloadGlobal() {
  Dl_info info;
  // Yes, dladdr returns non-zero on success...
  CHECK_NE(dladdr(reinterpret_cast<void*>(&LUAOPEN), &info), 0);
  CHECK(info.dli_fname);
  void* self = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_GLOBAL);
  CHECK(self);
  // ... but dlclose returns zero on success.
  CHECK_EQ(dlclose(self), 0);  // release this new reference
}

struct PythonInitializer {
  PythonInitializer();
};

PythonInitializer::PythonInitializer() {
  if (!Py_IsInitialized()) {
    Py_InitializeEx(0);  // no signal handlers
    PyEval_InitThreads();
    PyEval_ReleaseLock();
  }
}

std::mutex gPythonInitMutex;

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  reloadGlobal();
  // If you want a custom Python initialization; call it before loading this
  // module.
  static PythonInitializer initializer;  // only once, thread-safe

  lua_newtable(L);
  luaL_register(L, nullptr, pythonFuncs);

  {
    // numpy's import_array() doesn't appear to be thread-safe...

    std::lock_guard<std::mutex> lock(gPythonInitMutex);
    initRef(L);
    initLuaToPython(L);
    initPythonToLua(L);

    // Initialization finished. Any Python references created so far are there
    // to stay until the module is unloaded.
    debugSetWatermark();
  }

  return 1;
}
