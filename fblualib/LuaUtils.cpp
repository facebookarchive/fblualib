/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fblualib/LuaUtils.h>

#include <type_traits>
#include <boost/preprocessor/iteration/local.hpp>

namespace fblualib {

namespace detail {

void pushField(lua_State* L, int ud, const char* field) {
  if (lua_istable(L, ud)) {
    lua_getfield(L, ud, field);
  } else {
    lua_pushnil(L);
  }
}

void pushFieldChecked(lua_State* L, int ud, const char* field) {
  lua_getfield(L, ud, field);
  if (lua_isnil(L, -1)) {
    luaL_error(L, "No such field %s", field);
  }
}

}  // namespace detail

folly::Optional<folly::StringPiece> luaGetString(lua_State* L, int ud,
                                                 bool strict) {
  if (strict) {
    if (lua_type(L, ud) != LUA_TSTRING) return nullptr;
  } else {
    if (!lua_isstring(L, ud)) return nullptr;
  }

  size_t len = 0;
  const char* p = lua_tolstring(L, ud, &len);
  return folly::StringPiece(p, len);
}

folly::StringPiece luaGetStringChecked(lua_State* L, int ud, bool strict) {
  auto v = luaGetString(L, ud, strict);
  if (!v) {
    luaL_error(L, "not a string");
  }
  return *v;
}

folly::Optional<folly::StringPiece> luaGetFieldIfString(
    lua_State* L, int ud, const char* field, bool strict) {
  detail::pushField(L, ud, field);
  return luaGetString(L, -1, strict);
}

folly::StringPiece luaGetFieldIfStringChecked(lua_State* L, int ud,
                                              const char* field,
                                              bool strict) {
  detail::pushFieldChecked(L, ud, field);
  return luaGetStringChecked(L, -1, strict);
}

folly::Optional<bool> luaGetBoolean(lua_State* L, int ud, bool strict) {
  if (strict && lua_type(L, ud) != LUA_TBOOLEAN) {
    return nullptr;
  }
  return bool(lua_toboolean(L, ud));
}

bool luaGetBooleanChecked(lua_State* L, int ud, bool strict) {
  if (strict && lua_type(L, ud) != LUA_TBOOLEAN) {
    luaL_error(L, "not a boolean");
  }
  return bool(lua_toboolean(L, ud));
}

folly::Optional<bool> luaGetFieldIfBoolean(
    lua_State* L, int ud, const char* field, bool strict) {
  detail::pushField(L, ud, field);
  return luaGetBoolean(L, -1, strict);
}

bool luaGetFieldIfBooleanChecked(
    lua_State* L, int ud, const char* field, bool strict) {
  // Careful. In non-strict mode, nil is perfectly fine, so we'll call
  // pushField, not pushFieldChecked. (In strict mode, luaGetBooleanChecked
  // will error out on the nil anyway)
  detail::pushField(L, ud, field);
  return luaGetBooleanChecked(L, -1, strict);
}

folly::Optional<size_t> luaListSize(lua_State* L, int ud) {
  if (!lua_istable(L, ud)) {
    return nullptr;
  }
  size_t n = 0;
  bool end = false;
  for (; !end; ++n) {
    lua_rawgeti(L, ud, n + 1);
    end = lua_isnil(L, -1);
    lua_pop(L, 1);
  }
  return --n;
}

size_t luaListSizeChecked(lua_State* L, int ud) {
  auto v = luaListSize(L, ud);
  if (!v) {
    luaL_error(L, "not a table");
  }
  return *v;
}

// LuaJIT allows conversion from Lua files and FILE*, but only through FFI,
// which is incompatible with the standard Lua/C API. So, in Lua code,
// we encode the pointer as a Lua string and pass it down here.
FILE* luaDecodeFILE(lua_State* L, int index) {
  // Black magic. Don't look.
  auto filePtrData = luaGetStringChecked(L, index, true /*strict*/);
  luaL_argcheck(L,
                filePtrData.size() == sizeof(void*),
                index,
                "expected FILE* encoded as string");
  FILE* fp = nullptr;
  memcpy(&fp, filePtrData.data(), sizeof(void*));
  return fp;
}

void storePointerInRegistry(lua_State* L, const void* key, void* value) {
  lua_pushlightuserdata(L, const_cast<void*>(key));
  if (value) {
    lua_pushlightuserdata(L, value);
  } else {
    lua_pushnil(L);
  }
  lua_settable(L, LUA_REGISTRYINDEX);
}

void* loadPointerFromRegistry(lua_State* L, const void* key) {
  lua_pushlightuserdata(L, const_cast<void*>(key));
  lua_gettable(L, LUA_REGISTRYINDEX);
  void* value = nullptr;
  if (!lua_isnil(L, -1)) {
    DCHECK_EQ(lua_type(L, -1), LUA_TLIGHTUSERDATA);
    value = const_cast<void*>(lua_touserdata(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

int defaultCFunctionWrapper(lua_State* L, lua_CFunction fn) {
  try {
    return (*fn)(L);
  } catch (const std::exception& e) {
    luaPush(L, folly::exceptionStr(e));
    lua_error(L);
    return 0;  // unreached
  }
}

namespace {

void* getLUDUpValue(lua_State* L, int idx) {
  idx = lua_upvalueindex(idx);
  DCHECK_EQ(LUA_TLIGHTUSERDATA, lua_type(L, idx));
  auto r = lua_touserdata(L, idx);
  DCHECK(r);
  return r;
}

template <int N>
int trampoline(lua_State* L) {
  auto wrapper = reinterpret_cast<CFunctionWrapper>(getLUDUpValue(L, N + 1));
  auto fn = reinterpret_cast<lua_CFunction>(getLUDUpValue(L, N + 2));
  return (*wrapper)(L, fn);
}

// We reserve 2 upvalues for wrapper and actual function to be called
#define MAX_UPS (255 - 2)

lua_CFunction gTrampolines[] = {
#define BOOST_PP_LOCAL_LIMITS (0, MAX_UPS + 1)
#define BOOST_PP_LOCAL_MACRO(n) &trampoline<n>,
#include BOOST_PP_LOCAL_ITERATE()
#undef BOOST_PP_LOCAL_MACRO
#undef BOOST_PP_LOCAL_LIMITS
};

}

void pushWrappedCClosure(lua_State* L, lua_CFunction fn, int nups,
                         CFunctionWrapper wrapper) {
  // Push one of our wrapper functions instead, with an extra upvalue
  // indicating the C function to dispatch to.
  if (nups < 0 || nups > MAX_UPS) {
    luaL_error(L, "invalid upvalue count");
  }
  lua_pushlightuserdata(L, reinterpret_cast<void*>(wrapper));
  lua_pushlightuserdata(L, reinterpret_cast<void*>(fn));
  lua_pushcclosure(L, gTrampolines[nups], nups + 2);
}

void setWrappedFuncs(lua_State* L, const luaL_Reg* funcs, int nups,
                     CFunctionWrapper wrapper) {
  // table is at base
  // upvalues start at base + 1
  int base = lua_gettop(L) - nups;
  for (; funcs->name; ++funcs) {
    // Copy upvalues to the top
    for (int i = 1; i <= nups; ++i) {
      lua_pushvalue(L, base + i);
    }
    pushWrappedCClosure(L, funcs->func, nups, wrapper);
    lua_setfield(L, base, funcs->name);
  }
  lua_pop(L, nups);
}

}  // namespaces
