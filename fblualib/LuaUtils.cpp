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

}  // namespaces

