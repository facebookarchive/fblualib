/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fblualib/UserData.h>

#include <fblualib/LuaUtils.h>

namespace fblualib {

namespace detail {

// UserData implementation:
//
// The metatable for a userdata object of type Derived (with base class Base):
//
// We use a few special fields in the metatable:
//
// _methods points to the _methods table (searched by the index trampoline)
//
// _base points to the metatable of the base class
//
// _cast_to_base is a function that takes a void* (pointer to Derived) and
//     casts it to a pointer to Base
//
// +-----------------+------------------------------+
// | __XXX           | user-specified metamethod    |
// | __YYY           | user-specified metamethod    |
// | __index         | index trampoline             |
// | _methods        | methods table ---------------+-----+
// | _base           | metatable for base class     |     |
// | _cast_to_base   | cast function                |     |
// +-----------------+------------------------------+     |
//                                                        |
//      +-------------------------------------------------+
//      |
//      v
// +-----------------+------------------------------+
// | ZZZ             | user-specified method        |
// | UUU             | user-specified method        |
// | _index_handler_ | user-specified __index       |
// +-----------------+------------------------------+
//
// All metamethods from a base class are copied into the derived class's
// metatable.
//
// All methods from a base class are copied into the base class's methods
// table.
//
// Field lookup (a:foo) proceeds by first looking into the _methods table,
// then calling the _index_handler_ method if present.
//
// getUserData<T>(L, index) will walk the _base chain, calling _cast_to_base
// at each level, until the metatable of the object on the stack matches the
// metatable registered for type T.

// Upvalues:
// 1 = methods table
// 2 = index handler
//
// Called as __index, so arguments:
// 1 = object
// 2 = key
int indexTrampoline(lua_State* L) {
  lua_pushvalue(L, 2);
  lua_gettable(L, lua_upvalueindex(1));
  if (!lua_isnil(L, -1)) {
    return 1;  // found it in methods table, done
  }
  lua_pop(L, 1);

  // call index handler
  lua_pushvalue(L, lua_upvalueindex(2));
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

namespace {

// Copy all entries from the table at srcIdx to the table at destIdx
void copyTable(lua_State* L, int destIdx, int srcIdx) {
  destIdx = luaRealIndex(L, destIdx);
  srcIdx = luaRealIndex(L, srcIdx);
  DCHECK_NE(destIdx, srcIdx);

  lua_pushnil(L);
  while (lua_next(L, srcIdx)) {
    // k v
    lua_pushvalue(L, -2);
    // k v k
    lua_insert(L, -2);
    // k k v
    lua_settable(L, destIdx);
    // k
  }
}

}  // namespace

void doRegisterBase(lua_State* L, lua_CFunction castFunction) {
  // derived_mt derived_methods base_mt
  lua_getfield(L, -1, "_methods");

  // derived_mt derived_methods base_mt base_methods
  // Copy all metamethods and methods.
  copyTable(L, -3, -1);
  copyTable(L, -4, -2);

  // Add a _cast_to_base function in the metatable.
  lua_pushcfunction(L, castFunction);
  lua_setfield(L, -5, "_cast_to_base");

  // Add a link to base metatable
  lua_pushvalue(L, -2);
  lua_setfield(L, -5, "_base");

  // derived_mt derived_methods base_mt base_methods
  lua_pop(L, 2);
}

void* findClass(lua_State* L, void* ptr) {
  while (!lua_rawequal(L, -1, -2)) {
    // expected_mt actual_mt
    // Cast object to base
    lua_getfield(L, -1, "_cast_to_base");
    if (lua_isnil(L, -1)) {
      lua_pop(L, 3);
      return nullptr;
    }
    lua_pushlightuserdata(L, ptr);
    // expected_mt actual_mt cast_method ptr
    lua_call(L, 1, 1);
    // expected_mt actual_mt new_ptr
    ptr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "_base");
    if (lua_isnil(L, -1)) {
      // malformed?
      lua_pop(L, 3);
      return nullptr;
    }

    // expected_mt actual_mt base_mt
    lua_remove(L, -2);
    // expected_mt base_mt
  }
  lua_pop(L, 2);
  return ptr;
}

}  // namespace detail

}  // namespaces
