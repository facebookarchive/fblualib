/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <deque>

#include <fblualib/LuaUtils.h>
#include <fblualib/UserData.h>
#include <folly/io/async/EventBase.h>
#include <folly/MoveWrapper.h>

using namespace fblualib;

namespace {

class Reactor : public folly::Executor {
  enum : int {
    kImmediateCallbacksTable = 1,
    kDelayedCallbacksTable = 2,
  };
 public:
  enum LookupCallback : int {
    kNotFound = 0,
    kRunnable = 1,
    kDelayed = 2,
  };

  explicit Reactor(lua_State* L);
  ~Reactor();

  void add(folly::Func func) override;

  int luaLoop(lua_State* L);
  int luaAddCallback(lua_State* L);
  int luaAddCallbackDelayed(lua_State* L);
  int luaLookupCallback(lua_State* L);
  int luaRemoveCallback(lua_State* L);

  // Return a lightuserdata object that is a pointer to the corresponding
  // folly::Executor.
  int luaGetExecutor(lua_State* L);
  int luaGetEventBase(lua_State* L);

 private:
  int doLoop();

  int doAddCallback(int table);
  bool doRemoveCallback(int key, int table);
  bool doLookupCallback(int key, int table);
  void doAddDelayedCallback(int key);

  int seq_;
  lua_State* L_;
  std::unique_ptr<folly::EventBase> eb_;
};

Reactor::Reactor(lua_State* L)
  : seq_(0),
    L_(L),
    eb_(folly::make_unique<folly::EventBase>()) {
  lua_pushlightuserdata(L, this);
  lua_createtable(L, 2, 0);
  lua_newtable(L);
  lua_rawseti(L, -2, kImmediateCallbacksTable);
  lua_newtable(L);
  lua_rawseti(L, -2, kDelayedCallbacksTable);
  lua_settable(L, LUA_REGISTRYINDEX);
}

Reactor::~Reactor() {
  // This is fugly.
  //
  // The EventBase has pointers to this (via callbacks added using add()),
  // and it can run callbacks during destruction. So we must make sure to
  // drain it explicitly.
  //
  // Even more, we normally only execute callbacks once we get back to Lua
  // (via luaLoop), so even if we drained the EventBase here, those callbacks
  // would never get to run.
  //
  // So we have to drain it with a real Lua state.
  //
  // We'll first destroy the EventBase (running any callbacks that need to run
  // during destruction) then drain our callback tables.

  // First, make sure eb_ is nullptr, so that any attempt to use it (perhaps
  // by referring to this Reactor recursively) fails hard. Then, destroy the
  // EventBase.
  eb_.reset();

  // Now, run our callbacks.
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, kImmediateCallbacksTable);
  while (doLoop() != 0) { }
  lua_pop(L_, 1);

  lua_pushlightuserdata(L_, this);
  lua_pushnil(L_);
  lua_settable(L_, LUA_REGISTRYINDEX);
}

namespace {
int runFunc(lua_State* L) {
  auto fptr = static_cast<folly::Func*>(lua_touserdata(L, lua_upvalueindex(1)));
  SCOPE_EXIT {
    delete fptr;
  };
  (*fptr)();
  return 0;
}
}  // namespace

void Reactor::add(folly::Func func) {
  // We can't run the callback directly from the EventBase, because we
  // want Reactor to be reentrant; func may call back into Lua, which
  // may call loop() again, and EventBase doesn't like that. So we'll
  // unwind the stack and call the callbacks from loop().
  auto fptr = new folly::Func(std::move(func));
  eb_->add(
      [this, fptr] () {
        lua_pushlightuserdata(L_, fptr);
        lua_pushcclosure(L_, &runFunc, 1);
        doAddCallback(kImmediateCallbacksTable);
      });
}

int Reactor::luaAddCallbackDelayed(lua_State* L) {
  DCHECK(L == L_);
  if (!eb_) {
    luaL_error(
        L_, "Reactor being destroyed, delayed callbacks no longer allowed");
  }
  lua_pushvalue(L_, 3);
  int key = doAddCallback(kDelayedCallbacksTable);

  std::chrono::duration<double> seconds(luaGetChecked<double>(L, 2));
  auto millis =
    std::chrono::duration_cast<std::chrono::milliseconds>(seconds).count();

  eb_->runAfterDelay([this, key] () { doAddDelayedCallback(key); }, millis);

  lua_pushinteger(L_, key);
  return 1;
}

int Reactor::luaAddCallback(lua_State* L) {
  DCHECK(L == L_);
  lua_pushvalue(L_, 2);
  lua_pushinteger(L_, doAddCallback(kImmediateCallbacksTable));
  return 1;
}

bool Reactor::doRemoveCallback(int key, int table) {
  // tables
  lua_rawgeti(L_, -1, table);
  // tables table
  lua_rawgeti(L_, -1, key);
  // tables table old_cb
  if (lua_isnil(L_, -1)) {
    lua_pop(L_, 2);
    return false;
  }
  lua_pop(L_, 1);
  lua_pushnil(L_);
  // tables table nil
  lua_rawseti(L_, -2, key);
  // tables table
  lua_pop(L_, 1);
  return true;
}

int Reactor::luaRemoveCallback(lua_State* L) {
  DCHECK(L == L_);
  auto key = luaGetChecked<int>(L, 2);
  lua_pushlightuserdata(L, this);
  lua_gettable(L, LUA_REGISTRYINDEX);

  if (doRemoveCallback(key, kImmediateCallbacksTable)) {
    lua_pushinteger(L, kRunnable);
    return 1;
  }

  if (doRemoveCallback(key, kDelayedCallbacksTable)) {
    lua_pushinteger(L, kDelayed);
    return 1;
  }

  lua_pushinteger(L, kNotFound);
  return 1;
}

bool Reactor::doLookupCallback(int key, int table) {
  lua_rawgeti(L_, -1, table);
  // tables table
  lua_rawgeti(L_, -1, key);
  // tables table cb
  bool found = !lua_isnil(L_, -1);
  lua_pop(L_, 2);
  return found;
}

int Reactor::luaLookupCallback(lua_State* L) {
  DCHECK(L == L_);
  auto key = luaGetChecked<int>(L, 2);
  lua_pushlightuserdata(L, this);
  lua_gettable(L, LUA_REGISTRYINDEX);

  if (doLookupCallback(key, kImmediateCallbacksTable)) {
    lua_pushinteger(L, kRunnable);
    return 1;
  }

  if (doLookupCallback(key, kDelayedCallbacksTable)) {
    lua_pushinteger(L, kDelayed);
    return 1;
  }

  lua_pushinteger(L, kNotFound);
  return 1;
}

int Reactor::doAddCallback(int table) {
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, table);
  lua_remove(L_, -2);

  // cb run_table

  lua_insert(L_, -2);
  // run_table cb

  int key = ++seq_;
  lua_rawseti(L_, -2, key);
  // run_table

  lua_pop(L_, 1);

  return key;
}

void Reactor::doAddDelayedCallback(int key) {
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, kImmediateCallbacksTable);
  lua_rawgeti(L_, -2, kDelayedCallbacksTable);

  // move from delayed to immediate table

  // tables immediate delayed
  lua_rawgeti(L_, -1, key);
  // tables immediate delayed cb
  lua_pushnil(L_);
  // tables immediate delayed cb nil
  lua_rawseti(L_, -3, key);
  // tables immediate delayed cb
  lua_rawseti(L_, -3, key);
  // tables immediate delayed
  lua_pop(L_, 3);
}

int Reactor::luaLoop(lua_State* L) {
  DCHECK(L == L_);
  auto block = luaGetChecked<bool>(L, 2);
  int flags = block ? 0 : EVLOOP_NONBLOCK;
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, kImmediateCallbacksTable);
  int numCallbacks = 0;
  int top = lua_gettop(L_);

  do {
    if (!eb_->loopOnce(flags)) {
      luaL_error(L_, "EventBase loop returned error!");
    }

    DCHECK_EQ(top, lua_gettop(L_));
    numCallbacks += doLoop();
  } while (block && numCallbacks == 0);

  luaPush(L_, numCallbacks);
  return 1;
}

int Reactor::doLoop() {
  int numCallbacks = 0;
  for (;;) {
    // tab
    lua_pushnil(L_);
    // tab nil
    if (!lua_next(L_, -2)) {
      break;
    }
    // tab key value
    lua_insert(L_, -2);
    // tab value key
    lua_pushnil(L_);
    // tab value key nil
    lua_rawset(L_, -4);
    // tab value
    lua_call(L_, 0, 0);
    // tab
    ++numCallbacks;
  }
  return numCallbacks;
}

int Reactor::luaGetExecutor(lua_State* L) {
  lua_pushlightuserdata(L, static_cast<folly::Executor*>(this));
  return 1;
}

int Reactor::luaGetEventBase(lua_State* L) {
  lua_pushlightuserdata(L, eb_.get());
  return 1;
}

int luaNew(lua_State* L) {
  pushUserData<Reactor>(L, L);
  return 1;
}

const luaL_Reg gModuleFuncs[] = {
  {"new", luaNew},
  {nullptr, nullptr},
};

}  // namespace

namespace fblualib {

template <>
const UserDataMethod<Reactor> Metatable<Reactor>::metamethods[] = {
  {nullptr, nullptr},
};

template <>
const UserDataMethod<Reactor> Metatable<Reactor>::methods[] = {
  {"add_callback", &Reactor::luaAddCallback},
  {"add_callback_delayed", &Reactor::luaAddCallbackDelayed},
  {"lookup_callback", &Reactor::luaLookupCallback},
  {"remove_callback", &Reactor::luaRemoveCallback},
  {"loop", &Reactor::luaLoop},
  {"get_executor", &Reactor::luaGetExecutor},
  {"get_event_base", &Reactor::luaGetEventBase},
  {nullptr, nullptr},
};

}  // namespace fblualib

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, gModuleFuncs);

  lua_pushinteger(L, Reactor::kNotFound);
  lua_setfield(L, -2, "NOT_FOUND");

  lua_pushinteger(L, Reactor::kRunnable);
  lua_setfield(L, -2, "RUNNABLE");

  lua_pushinteger(L, Reactor::kDelayed);
  lua_setfield(L, -2, "DELAYED");

  return 1;
}
