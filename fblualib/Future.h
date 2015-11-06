/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUALIB_FUTURE_H_
#define FBLUALIB_FUTURE_H_

#include <lua.hpp>

#include <fblualib/LuaUtils.h>
#include <fblualib/Reactor.h>
#include <folly/String.h>

namespace fblualib {

/**
 * C++ interface to the producer (Promise) side of fb.util.future.
 *
 * This allows C++ code to create promises and fulfill them later.
 *
 * This integrates with fb.util.reactor. If you get the reactor's executor
 * (by calling its get_executor() method) and schedule promise fulfillment
 * in that executor, then Lua code can wait for the corresponding futures
 * to complete using the reactor's await() method.
 *
 * DO NOT CAPTURE THE lua_State* IN THE FUNCTIONS THAT YOU SCHEDULE IN THE
 * REACTOR'S EXECUTOR. Use loopingState().L instead. See <fblualib/Reactor.h>
 * for more details.
 */
class Promise {
 public:
  Promise() : key_(0) { }

  /**
   * Create a promise, leave the associated future on the stack, and
   * return a key that you may use to refer to the promise in setPromiseValue /
   * setPromiseError later.
   *
   * Note that the promise is anchored (and won't be garbage collected) until
   * you call setPromiseValue / setPromiseError. If numAnchored > 0, then
   * numAnchored elements are popped from the stack and anchored as well.
   * Anchoring is most useful for userdata objects where you have a (C)
   * pointer to the object, but they might be GCed by the time of completion.
   *
   * The promise is associated with a lua_State until fulfilled. You may only
   * call Promise methods when running in the context of that lua_State!
   * Promise methods take a lua_State* as the first argument, and will crash
   * if that doesn't match the lua_State that the Promise is associated with.
   */
  static Promise create(lua_State* L, int numAnchored = 0);
  ~Promise();

  Promise(Promise&& other) noexcept;
  Promise& operator=(Promise&& other);  /* noexcept override */

  Promise(const Promise&) = delete;
  Promise& operator=(const Promise&) = delete;

  /**
   * Fulfill the promise by setting the value to the top n elements of
   * the stack (multiple return values); the n elements are popped.
   */
  void setValue(lua_State* L, int n = 1);

  /**
   * Fulfill the promise by setting the error to the top element of the stack.
   * (which is popped).
   */
  void setError(lua_State* L);

  /**
   * Fulfill the promise by setting the error to a string.
   */
  void setErrorFrom(lua_State* L, folly::StringPiece sp) {
    luaPush(L, sp);
    setError(L);
  }

  /**
   * Fulfill the promise by setting the error to an appropriate message
   * for the given C++ exception.
   */
  void setErrorFrom(lua_State* L, const std::exception& exc) {
    setErrorFrom(L, folly::exceptionStr(exc));
  }

 private:
  explicit Promise(uint64_t key);

  void validate(lua_State* L);
  void callPromiseMethod(lua_State* L, const char* method, int n);

  uint64_t key_;
};

/**
 * Initialization. Call before using, for each lua_State that you intend
 * to use this in.
 */
void initFuture(lua_State* L);

}  // namespaces

#endif /* FBLUALIB_FUTURE_H_ */
