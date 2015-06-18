/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fblualib/Future.h>

namespace fblualib {

namespace {

char kPromiseRegistryKey;

}  // namespace

Promise Promise::create(lua_State* L, int numAnchored) {
  lua_pushlightuserdata(L, &kPromiseRegistryKey);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_getfield(L, -1, "create");
  // up1 up2... upN mod create_func
  lua_insert(L, -2 - numAnchored);
  // create_func up1 up2 ... upN mod
  lua_pop(L, 1);
  // create_func up1 up2 ... upN
  lua_call(L, numAnchored, 2);

  // future key
  Promise promise(L, luaGetChecked<uint64_t>(L, -1));
  lua_pop(L, 1);

  // future, left on the stack

  return promise;
}

Promise::Promise(Promise&& other) noexcept
  : L_(other.L_),
    key_(other.key_) {
  other.L_ = nullptr;
  other.key_ = 0;
}

Promise& Promise::operator=(Promise&& other) {
  if (this != &other) {
    CHECK(!L_) << "Promise overwritten without being fulfilled";
    L_ = other.L_;
    key_ = other.key_;
    other.L_ = nullptr;
    other.key_ = 0;
  }
  return *this;
}

Promise::~Promise() {
  CHECK(!L_) << "Promise destroyed without being fulfilled";
}

void Promise::validate(lua_State* L) {
  if (!L_) {
    throw std::logic_error("Promise is empty (already fulfilled)");
  }
  // Crash if called with wrong lua_State
  CHECK(L == L_);
}

void Promise::setValue(lua_State* L, int n) {
  callPromiseMethod(L, "set_value", n);
}

void Promise::setError(lua_State* L) {
  callPromiseMethod(L, "set_error", 1);
}

void Promise::callPromiseMethod(lua_State* L, const char* method, int n) {
  validate(L);
  lua_pushlightuserdata(L, &kPromiseRegistryKey);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_getfield(L, -1, method);
  // arg1 arg2 ... argn mod method
  lua_insert(L_, -2 - n);
  lua_pop(L_, 1);
  // method arg1 arg2 ... argn
  luaPush(L_, key_);
  // method arg1 arg2 ... argn key
  lua_insert(L_, -1 - n);
  // method key arg1 arg2 ... argn
  lua_call(L_, n + 1, 0);
  L_ = nullptr;
  key_ = 0;
}

void initFuture(lua_State* L) {
  lua_pushlightuserdata(L, &kPromiseRegistryKey);

  lua_getglobal(L, "require");
  lua_pushstring(L, "fb.util._promise_registry");
  lua_call(L, 1, 1);

  lua_settable(L, LUA_REGISTRYINDEX);
}

}  // namespaces
