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

#pragma once

#include <lua.hpp>
#include <folly/Executor.h>

namespace fblualib {

struct LoopingState {
  explicit LoopingState(lua_State* L = nullptr, folly::Executor* ex = nullptr)
    : L(L), executor(ex) { }

  lua_State* L;
  folly::Executor* executor;
};

namespace detail {
extern thread_local LoopingState gLoopingState;
}  // namespace detail

/**
 * If there is a Reactor currently looping in this thread (using loop()),
 * return it.
 *
 * This is most useful for functions added using the Reactor's folly::Executor
 * interface. DO NOT CAPTURE THE CURRENT lua_State* WHEN SCHEDULING THESE
 * FUNCTIONS; the Reactor might loop in a different coroutine (with a different
 * lua_State*) and so you'll end up using the wrong lua_State.
 *
 * auto ex = ....;  // get a pointer to the executor somehow
 * lua_State* L;
 *
 * DO NOT DO THIS:
 *
 * ! folly::via(ex)
 * !   .then(
 * !     [L] {
 * !       lua_pushinteger(L, 42);
 * !       lua_setglobal(L, "foo");
 * !     });
 *
 * DO THIS INSTEAD:
 *
 *   folly::via(ex)
 *     .then(
 *       [] {
 *         auto L = loopingState().L;
 *         lua_pushinteger(L, 42);
 *         lua_setglobal(L, "foo")p;
 *       });
 *
 * (loopingState().executor should be the same as ex, feel free to assert that)
 */
inline const LoopingState& loopingState() { return detail::gLoopingState; }

}  // namespaces
