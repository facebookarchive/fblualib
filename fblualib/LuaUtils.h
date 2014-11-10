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
#define FBLUA_LUAUTILS_H_

#include <lua.hpp>

#include <thpp/Tensor.h>
#include <folly/Optional.h>
#include <folly/Range.h>

namespace fblualib {

// Functions without a Checked suffix return an Optional<T> for an appropriate
// T. The Optional will be empty on failure. Functions with a Checked suffix
// return T and raise a Lua error on failure.
//
// Functions without GetFieldIf in the name return the object at the
// requested index on the lua stack, and leave the stack unchanged. Functions
// with GetFieldIf return the object from a field in the table at the
// requested index, and also push the returned object on the stack.
//
// Note that the GetString functions may return references to the stack.

// Retrieve string.
// strict = require that it's an actual string (and not a number
//          convertible to a string)
// Even though those functions return StringPiece (which is usually not
// null-terminated), these functions guarantee that the returned string is
// null-terminated.
folly::Optional<folly::StringPiece> luaGetString(lua_State* L, int ud,
                                                 bool strict=false);
folly::StringPiece luaGetStringChecked(lua_State* L, int ud,
                                       bool strict=false);
folly::Optional<folly::StringPiece> luaGetFieldIfString(lua_State* L, int ud,
                                                        bool strict=false);
folly::StringPiece luaGetFieldIfStringChecked(lua_State* L, int ud,
                                              const char* field,
                                              bool strict=false);

// Retrieve number
// strict = require that it's an actual number (and not a string
//          convertible to a number)
template <class NT>
folly::Optional<NT> luaGetNumber(lua_State* L, int ud, bool strict=false);
template <class NT>
NT luaGetNumberChecked(lua_State* L, int ud, bool strict=false);
template <class NT>
folly::Optional<NT> luaGetFieldIfNumber(
    lua_State* L, int ud, const char* field, bool strict=false);
template <class NT>
NT luaGetFieldIfNumberChecked(lua_State* L, int ud, const char* field,
                              bool strict=false);

// Retrieve boolean
// In strict mode, we only accept booleans (true or false)
// In non-strict mode, nil and false cause us to return false, everything
// else causes us to return true.
//
// This means that, in non-strict mode, luaGetBoolean will always return
// a value, and luaGetBooleanChecked will not error out.
//
// This also means that, in non-strict mode, luaGetFieldIfBoolean and
// luaGetFieldIfBooleanChecked will return a value (false) if the field
// doesn't exist; this is a side effect of the fact that Lua doesn't let us
// store nil in tables.
folly::Optional<bool> luaGetBoolean(lua_State* L, int ud, bool strict=false);
bool luaGetBooleanChecked(lua_State* L, int ud, bool strict=false);
folly::Optional<bool> luaGetFieldIfBoolean(
    lua_State* L, int ud, const char* field, bool strict=false);
bool luaGetFieldIfBooleanChecked(
    lua_State* L, int ud, const char* field, bool strict=false);

// Retrieve tensor
template <class NT>
folly::Optional<thpp::Tensor<NT>> luaGetTensor(lua_State* L, int ud);
template <class NT>
thpp::Tensor<NT> luaGetTensorChecked(lua_State* L, int ud);
template <class NT>
folly::Optional<thpp::Tensor<NT>> luaGetFieldIfTensor(
    lua_State* L, int ud, const char* field);
template <class NT>
thpp::Tensor<NT> luaGetFieldIfTensorChecked(lua_State* L, int ud,
                                            const char* field);

// Push a tensor onto the stack
template <class NT>
void luaPushTensor(lua_State* L, thpp::Tensor<NT> tensor);

// Retrieve storage
template <class NT>
folly::Optional<thpp::Storage<NT>> luaGetStorage(lua_State* L, int ud);
template <class NT>
thpp::Storage<NT> luaGetStorageChecked(lua_State* L, int ud);
template <class NT>
folly::Optional<thpp::Storage<NT>> luaGetFieldIfStorage(
    lua_State* L, int ud, const char* field);
template <class NT>
thpp::Storage<NT> luaGetFieldIfStorageChecked(
    lua_State* L, int ud, const char* field);

// Push a storage onto the stack
template <class NT>
void luaPushStorage(lua_State* L, thpp::Storage<NT> storage);

// Return the length of a list-like table at the given stack index.
// Different from lua_objlen in that it guarantees that it will return the
// smallest N for which all indexes i, 1 <= i <= N, exist in the table,
// but O(N) (as it iterates through the table), unlike lua_objlen, which may
// be faster.
folly::Optional<size_t> luaListSize(lua_State* L, int ud);
size_t luaListSizeChecked(lua_State* L, int ud);

// Get a FILE* encoded as a Lua string.
FILE* luaDecodeFILE(lua_State* L, int index);

}  // namespaces

#include <fblualib/LuaUtils-inl.h>

#endif /* FBLUA_LUAUTILS_H_ */

