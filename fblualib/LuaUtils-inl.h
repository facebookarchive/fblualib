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
folly::Optional<typename thpp::Tensor<T>::Ptr> luaGetTensor(
    lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Tensor<T>::THType*>(
      luaT_toudata(L, ud, thpp::Tensor<T>::kLuaTypeName));
  folly::Optional<typename thpp::Tensor<T>::Ptr> opt;
  if (p) {
    opt.emplace(p);
  }
  return opt;
}

template <class T>
typename thpp::Tensor<T>::Ptr luaGetTensorChecked(lua_State* L, int ud) {
  auto p = static_cast<typename thpp::Tensor<T>::THType*>(
      luaT_toudata(L, ud, thpp::Tensor<T>::kLuaTypeName));
  if (!p) {
    luaL_error(L, "Not a valid tensor");
  }
  return typename thpp::Tensor<T>::Ptr(p);
}

template <class T>
folly::Optional<typename thpp::Tensor<T>::Ptr> luaGetFieldIfTensor(
    lua_State* L, int ud, const char* field) {
  detail::pushField(L, ud, field);
  return luaGetTensor<T>(L, -1);
}

template <class T>
typename thpp::Tensor<T>::Ptr luaGetFieldIfTensorChecked(
    lua_State* L, int ud, const char* field) {
  detail::pushFieldChecked(L, ud, field);
  return luaGetTensorChecked<T>(L, -1);
}

template <class T>
void luaPushTensor(lua_State* L, thpp::TensorPtr<thpp::Tensor<T>> tensor) {
  luaT_pushudata(L, tensor.moveAsTH(), thpp::Tensor<T>::kLuaTypeName);
}

template <class T>
void luaPushTensor(lua_State* L, const thpp::Tensor<T>& tensor) {
  luaPushTensor<T>(L, tensor.copyPtr());
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

namespace detail {

template <class T, class Enable=void> struct LuaOp;

template <class T, class Derived> struct LuaOpBase {
  inline static T getChecked(lua_State* L, int index) {
    auto opt = Derived::get(L, index);
    if (!opt) {
      throw std::invalid_argument("Invalid object");
    }
    return std::move(*opt);
  }
};

template <>
struct LuaOp<bool> {
  inline static void push(lua_State* L, bool value) {
    lua_pushboolean(L, value);
  }
  inline static bool getChecked(lua_State* L, int index) {
    return lua_toboolean(L, index);
  }
  inline static folly::Optional<bool> get(lua_State* L, int index) {
    return getChecked(L, index);
  }
};

template <class T>
struct LuaOp<
  T,
  typename std::enable_if<
    (std::is_integral<T>::value && !std::is_same<T, bool>::value)
  >::type> : public LuaOpBase<T, LuaOp<T>> {
  inline static void push(lua_State* L, T value) {
    // convert at runtime, throw if value overflows
    lua_pushinteger(L, folly::to<lua_Integer>(value));
  }
  inline static folly::Optional<T> get(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TNUMBER) return folly::none;
    return folly::to<T>(lua_tointeger(L, index));
  }
};

template <class T>
struct LuaOp<
  T,
  typename std::enable_if<
    std::is_same<T, float>::value ||
    std::is_same<T, double>::value>::type>
  : public LuaOpBase<T, LuaOp<T>> {
  inline static void push(lua_State* L, T value) {
    lua_pushnumber(L, value);
  }
  inline static folly::Optional<T> get(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TNUMBER) return folly::none;
    return folly::to<T>(lua_tonumber(L, index));
  }
};

template <class T>
struct LuaOp<
  T,
  typename std::enable_if<
    (folly::IsSomeString<T>::value ||
     std::is_same<T, folly::StringPiece>::value)>::type>
  : public LuaOpBase<T, LuaOp<T>> {
  inline static void push(lua_State* L, const T& value) {
    lua_pushlstring(L, value.data(), value.size());
  }
  inline static folly::Optional<T> get(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TSTRING) return folly::none;
    size_t len;
    auto s = lua_tolstring(L, index, &len);
    return T(s, len);
  }
};

template <>
struct LuaOp<const char*> : public LuaOpBase<const char*, LuaOp<const char*>> {
  inline static void push(lua_State* L, const char* value) {
    lua_pushstring(L, value);
  }
  inline static folly::Optional<const char*> get(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TSTRING) return folly::none;
    return lua_tostring(L, index);
  }
};

template <class T>
struct LuaOp<
  T,
  typename std::enable_if<
    (thpp::IsTensorPtr<T>::value || thpp::IsStorage<T>::value)>::type>
  : public LuaOpBase<T, LuaOp<T>> {
  inline static void push(lua_State* L, T value) {
    luaT_pushudata(L, value.moveAsTH(), T::kLuaTypeName);
  }
  inline static folly::Optional<T> get(lua_State* L, int index) {
    auto p = static_cast<typename T::THType*>(
        luaT_toudata(L, index, T::kLuaTypeName));
    folly::Optional<T> opt;
    if (p) {
      opt.emplace(p);
    }
    return opt;
  }
};

template <class T>
struct LuaOp<
  T,
  typename std::enable_if<thpp::IsTensor<T>::value>::type>
  : public LuaOpBase<T, LuaOp<T>> {
  inline static void push(lua_State* L, const T& value) {
    LuaOp<typename T::Ptr>::push(L, value.copyPtr());
  }
  inline static folly::Optional<T> get(lua_State* L, int index) {
    folly::Optional<T> opt;
    auto p = LuaOp<typename T::Ptr>::get(L, index);
    if (p) {
      opt.emplace(**p);
    }
    return opt;
  }
};

}  // namespace detail

template <class T>
inline void luaPush(lua_State* L, T&& obj) {
  detail::LuaOp<typename std::decay<T>::type>::push(L, std::forward<T>(obj));
}

template <class T>
inline folly::Optional<T> luaGet(lua_State* L, int index) {
  return detail::LuaOp<T>::get(L, index);
}

template <class T>
inline T luaGetChecked(lua_State* L, int index) {
  return detail::LuaOp<T>::getChecked(L, index);
}

}  // namespaces
