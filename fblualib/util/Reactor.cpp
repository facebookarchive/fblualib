/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <vector>

#include <fblualib/LuaUtils.h>
#include <fblualib/Reactor.h>
#include <fblualib/UserData.h>
#include <folly/io/async/EventBase.h>
#include <folly/MoveWrapper.h>

namespace fblualib {

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
  int luaGC(lua_State* L);

 private:
  int doLoop(lua_State* L);

  void pushTable(lua_State* L, int table);
  int doAddCallback(lua_State* L);
  bool doLookupOrRemoveCallback(lua_State* L, int key, bool remove);
  int doLuaLookupOrRemoveCallback(lua_State* L, bool remove);
  void doAddDelayedCallback(lua_State* L, int key);

  int seq_;
  std::unique_ptr<folly::EventBase> eb_;
};

Reactor::Reactor(lua_State* L)
  : seq_(0),
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
  DCHECK(!eb_);  // luaGC must have been called
}

int Reactor::luaGC(lua_State* L) {
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

  auto prevLoopingState = detail::gLoopingState;
  SCOPE_EXIT {
    detail::gLoopingState = prevLoopingState;
  };
  detail::gLoopingState = LoopingState(L, this);

  eb_.reset();

  // Now, run our callbacks.
  pushTable(L, kImmediateCallbacksTable);
  while (doLoop(L) != 0) { }
  lua_pop(L, 1);

  lua_pushlightuserdata(L, this);
  lua_pushnil(L);
  lua_settable(L, LUA_REGISTRYINDEX);

  return 0;
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
  if (!eb_) {
    // We're trying to add callbacks recursively during destruction.
    // This all stems from the fact that EventBase lets you do such horrible
    // things. There's absolutely nothing good we can do here, so we'll
    // do exactly what EventBase does -- ignore.
    return;
  }

  // We can't run the callback directly from the EventBase, because we
  // want Reactor to be reentrant; func may call back into Lua, which
  // may call loop() again, and EventBase doesn't like that. So we'll
  // unwind the stack and call the callbacks from loop().
  auto fptr = new folly::Func(std::move(func));
  eb_->add(
      [this, fptr] () {
        auto& ls = loopingState();
        DCHECK(ls.L);
        DCHECK(ls.executor == this);
        auto L = ls.L;
        pushTable(L, kImmediateCallbacksTable);
        lua_pushlightuserdata(L, fptr);
        pushWrappedCClosure(L, &runFunc, 1);
        doAddCallback(L);
        lua_pop(L, 1);
      });
}

int Reactor::luaAddCallbackDelayed(lua_State* L) {
  if (!eb_) {
    luaL_error(
        L, "Reactor being destroyed, delayed callbacks no longer allowed");
  }
  pushTable(L, kDelayedCallbacksTable);
  lua_pushvalue(L, 3);
  int key = doAddCallback(L);
  lua_pop(L, 1);

  std::chrono::duration<double> seconds(luaGetChecked<double>(L, 2));
  auto millis =
    std::chrono::duration_cast<std::chrono::milliseconds>(seconds).count();

  eb_->runAfterDelay(
      [this, key] () {
        auto& ls = loopingState();
        DCHECK(ls.L);
        DCHECK(ls.executor == this);
        doAddDelayedCallback(ls.L, key);
      },
      millis);

  lua_pushinteger(L, key);
  return 1;
}

int Reactor::luaAddCallback(lua_State* L) {
  pushTable(L, kImmediateCallbacksTable);
  lua_pushvalue(L, 2);
  lua_pushinteger(L, doAddCallback(L));
  return 1;
}

bool Reactor::doLookupOrRemoveCallback(lua_State* L, int key, bool remove) {
  // table
  lua_rawgeti(L, -1, key);
  // table cb
  bool found = !lua_isnil(L, -1);
  lua_pop(L, 1);
  if (found && remove) {
    lua_pushnil(L);
    lua_rawseti(L, -2, key);
  }
  return found;
}

int Reactor::doLuaLookupOrRemoveCallback(lua_State* L, bool remove) {
  auto key = luaGetChecked<int>(L, 2);

  pushTable(L, kImmediateCallbacksTable);
  if (doLookupOrRemoveCallback(L, key, remove)) {
    lua_pushinteger(L, kRunnable);
    return 1;
  }
  lua_pop(L, 1);

  pushTable(L, kDelayedCallbacksTable);
  if (doLookupOrRemoveCallback(L, key, remove)) {
    lua_pushinteger(L, kDelayed);
    return 1;
  }
  lua_pop(L, 1);

  lua_pushinteger(L, kNotFound);
  return 1;
}

int Reactor::luaRemoveCallback(lua_State* L) {
  return doLuaLookupOrRemoveCallback(L, true);
}

int Reactor::luaLookupCallback(lua_State* L) {
  return doLuaLookupOrRemoveCallback(L, false);
}

void Reactor::pushTable(lua_State* L, int table) {
  lua_pushlightuserdata(L, this);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_rawgeti(L, -1, table);
  lua_remove(L, -2);
}

int Reactor::doAddCallback(lua_State* L) {
  // run_table cb
  int key = ++seq_;
  lua_rawseti(L, -2, key);
  // run_table
  return key;
}

void Reactor::doAddDelayedCallback(lua_State* L, int key) {
  lua_pushlightuserdata(L, this);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_rawgeti(L, -1, kImmediateCallbacksTable);
  lua_rawgeti(L, -2, kDelayedCallbacksTable);

  // move from delayed to immediate table

  // tables immediate delayed
  lua_rawgeti(L, -1, key);
  // tables immediate delayed cb
  lua_pushnil(L);
  // tables immediate delayed cb nil
  lua_rawseti(L, -3, key);
  // tables immediate delayed cb
  lua_rawseti(L, -3, key);
  // tables immediate delayed
  lua_pop(L, 3);
}

int Reactor::luaLoop(lua_State* L) {
  auto block = luaGetChecked<bool>(L, 2);
  int flags = block ? 0 : EVLOOP_NONBLOCK;

  pushTable(L, kImmediateCallbacksTable);
  int numCallbacks = 0;
  int top = lua_gettop(L);

  auto prevLoopingState = detail::gLoopingState;
  SCOPE_EXIT {
    detail::gLoopingState = prevLoopingState;
  };
  detail::gLoopingState = LoopingState(L, this);

  do {
    if (!eb_->loopOnce(flags)) {
      luaL_error(L, "EventBase loop returned error!");
    }

    DCHECK_EQ(top, lua_gettop(L));
    numCallbacks += doLoop(L);
  } while (block && numCallbacks == 0);

  luaPush(L, numCallbacks);
  return 1;
}

int Reactor::doLoop(lua_State* L) {
  int numCallbacks = 0;
  for (;;) {
    // tab
    lua_pushnil(L);
    // tab nil
    if (!lua_next(L, -2)) {
      break;
    }
    // tab key value
    lua_insert(L, -2);
    // tab value key
    lua_pushnil(L);
    // tab value key nil
    lua_rawset(L, -4);
    // tab value
    lua_call(L, 0, 0);
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

template <>
const UserDataMethod<Reactor> Metatable<Reactor>::methods[] = {
  {"add_callback", &Reactor::luaAddCallback},
  {"add_callback_delayed", &Reactor::luaAddCallbackDelayed},
  {"lookup_callback", &Reactor::luaLookupCallback},
  {"remove_callback", &Reactor::luaRemoveCallback},
  {"loop", &Reactor::luaLoop},
  {"get_executor", &Reactor::luaGetExecutor},
  {"get_event_base", &Reactor::luaGetEventBase},
  {"__gc", &Reactor::luaGC},
  {nullptr, nullptr},
};

}  // namespace fblualib

using namespace fblualib;

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
