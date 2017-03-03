/**
 * Copyright 2015 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/Future.h>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <fblualib/LuaUtils.h>
#include <folly/futures/Future.h>

namespace fblualib { namespace test {

LuaStatePtr GL;

const char kReactorSetUpCode[] =
  "local reactor = require('fb.util.reactor')\n"
  "\n"
  "local R = reactor.Reactor()\n"
  "return R, R:get_executor()\n";

folly::Executor* setUpReactor() {
  auto L = GL.get();
  auto r = luaL_loadstring(L, kReactorSetUpCode);
  CHECK_EQ(r, 0);
  lua_call(L, 0, 2);
  auto executor = static_cast<folly::Executor*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return executor;
}

TEST(PromiseTest, SuccessfulFulfillment) {
  auto L = GL.get();
  auto executor = setUpReactor();
  // reactor
  lua_getfield(L, -1, "awaitv");
  lua_insert(L, -2);
  // await reactor
  auto promise = Promise::create(L);
  // await reactor future
  folly::via(executor)
    .then(
        [&promise] () {
          auto L = loopingState().L;
          luaPush(L, 42);
          luaPush(L, 100);
          promise.setValue(L, 2);
        });
  int r = lua_pcall(L, 2, 2, 0);
  EXPECT_EQ(0, r);
  if (r != 0) {
    LOG(ERROR) << luaGetChecked<folly::StringPiece>(L, -1);
  }
  EXPECT_EQ(42, luaGetChecked<int>(L, -2));
  EXPECT_EQ(100, luaGetChecked<int>(L, -1));
}

TEST(PromiseTest, ErrorFulfillment) {
  auto L = GL.get();
  auto executor = setUpReactor();
  // reactor
  lua_getfield(L, -1, "awaitv");
  lua_insert(L, -2);
  // await reactor
  auto promise = Promise::create(L);
  // await reactor promise
  folly::via(executor)
    .then(
        [&promise] () {
          auto L = loopingState().L;
          promise.setErrorFrom(L, "hello");
        });
  int r = lua_pcall(L, 2, 0, 0);
  EXPECT_EQ(LUA_ERRRUN, r);
  EXPECT_TRUE(luaGetChecked<folly::StringPiece>(L, -1).find("hello") !=
              folly::StringPiece::npos);
}

}}  // namespaces

using namespace fblualib;
using namespace fblualib::test;

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  initLuaEmbedding();
  GL = luaNewState();
  auto L = GL.get();
  luaL_openlibs(L);

  initFuture(L);

  return RUN_ALL_TESTS();
}
