/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_LUAUTILS_H_
#error This file may only be included from fblualib/LuaUtils.h
#endif

#include <type_traits>

#include <luaT.h>

namespace fblualib {

namespace detail {

template <class T, class Enable=void> struct ToNumber;

template <class T>
struct ToNumber<T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static folly::Optional<T> get(lua_State* L, int ud, bool strict) {
    if (strict) {
      if (lua_type(L, ud) != LUA_TNUMBER) return nullptr;
    } else {
      if (!lua_isnumber(L, ud)) return nullptr;
    }
    return lua_tonumber(L, ud);
  }
};

template <class T>
struct ToNumber<T,
    typename std::enable_if<std::is_integral<T>::value>::type> {
  static folly::Optional<T> get(lua_State* L, int ud, bool strict) {
    if (strict) {
      if (lua_type(L, ud) != LUA_TNUMBER) return nullptr;
    } else {
      if (!lua_isnumber(L, ud)) return nullptr;
    }
    return lua_tointeger(L, ud);
  }
};

void pushField(lua_State* L, int ud, const char* field);
void pushFieldChecked(lua_State* L, int ud, const char* field);

}  // namespace detail

template <class T>
folly::Optional<T> luaGetNumber(lua_State* L, int ud, bool strict) {
  return detail::ToNumber<T>::get(L, ud, strict);
}

template <class T>
T luaGetNumberChecked(lua_State* L, int ud, bool strict) {
  auto v = luaGetNumber<T>(L, ud, strict);
  if (!v) {
    luaL_error(L, "Not a number");
  }
  return *v;
}

template <class T>
folly::Optional<T> luaGetFieldIfNumber(lua_State* L, int ud,
                                       const char* field,
                                       bool strict) {
  detail::pushField(L, ud, field);
  return luaGetNumber<T>(L, -1, strict);
}

template <class T>
T luaGetFieldIfNumberChecked(lua_State* L, int ud, const char* field,
                             bool strict) {
  detail::pushFieldChecked(L, ud, field);
  return luaGetNumberChecked<T>(L, -1, strict);
}

template <class T>
folly::Optional<thpp::Tensor<T>> luaGetTensor(lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Tensor<T>::THType*>(
      luaT_toudata(L, ud, thpp::Tensor<T>::kLuaTypeName));
  folly::Optional<thpp::Tensor<T>> opt;
  if (p) {
    opt = thpp::Tensor<T>(p, thpp::TensorMustAlias());
  }
  return opt;
}

template <class T>
thpp::Tensor<T> luaGetTensorChecked(lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Tensor<T>::THType*>(
      luaT_toudata(L, ud, thpp::Tensor<T>::kLuaTypeName));
  if (!p) {
    luaL_error(L, "Not a valid tensor");
  }
  return thpp::Tensor<T>(p, thpp::TensorMustAlias());
}

template <class T>
folly::Optional<thpp::Tensor<T>> luaGetFieldIfTensor(
    lua_State* L, int ud, const char* field) {
  detail::pushField(L, ud, field);
  return luaGetTensor<T>(L, -1);
}

template <class T>
thpp::Tensor<T> luaGetFieldIfTensorChecked(lua_State* L, int ud,
                                           const char* field) {
  detail::pushFieldChecked(L, ud, field);
  return luaGetTensorChecked<T>(L, -1);
}

template <class T>
void luaPushTensor(lua_State* L, thpp::Tensor<T> tensor) {
  luaT_pushudata(L, tensor.moveAsTH(), thpp::Tensor<T>::kLuaTypeName);
}

template <class T>
folly::Optional<thpp::Storage<T>> luaGetStorage(lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Storage<T>::THType*>(
      luaT_toudata(L, ud, thpp::Storage<T>::kLuaTypeName));
  folly::Optional<thpp::Storage<T>> opt;
  if (p) {
    opt = thpp::Storage<T>(p);
  }
  return opt;
}

template <class T>
thpp::Storage<T> luaGetStorageChecked(lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Storage<T>::THType*>(
      luaT_toudata(L, ud, thpp::Storage<T>::kLuaTypeName));
  if (!p) {
    luaL_error(L, "Not a valid storage");
  }
  return thpp::Storage<T>(p);
}

template <class T>
folly::Optional<thpp::Storage<T>> luaGetFieldIfStorage(
    lua_State* L, int ud, const char* field) {
  detail::pushField(L, ud, field);
  return luaGetStorage<T>(L, -1);
}

template <class T>
thpp::Storage<T> luaGetFieldIfStorageChecked(
    lua_State* L, int ud, const char* field) {
  detail::pushFieldChecked(L, ud, field);
  return luaGetStorageChecked<T>(L, -1);
}

template <class T>
void luaPushStorage(lua_State* L, thpp::Storage<T> storage) {
  luaT_pushudata(L, storage.moveAsTH(), thpp::Storage<T>::kLuaTypeName);
}

}  // namespaces
