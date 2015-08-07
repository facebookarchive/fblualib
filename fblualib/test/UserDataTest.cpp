/**
 * Copyright 2015 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/UserData.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace fblualib { namespace test {

LuaStatePtr GL;

class TestObject {
 public:
  static bool destructorCalled;

  explicit TestObject(int x) : x(x), y(0) { }
  ~TestObject() { destructorCalled = true; }

  int luaLen(lua_State* L);
  int luaValue(lua_State* L);
  int luaIndex(lua_State* L);
  int luaNewIndex(lua_State* L);

  int x;
  int y;
};

int TestObject::luaLen(lua_State* L) {
  luaPush(L, 10);
  return 1;
}

int TestObject::luaValue(lua_State* L) {
  luaPush(L, x);
  return 1;
}

int TestObject::luaIndex(lua_State* L) {
  auto arg = luaGet<folly::StringPiece>(L, 2);
  if (arg && *arg == "y") {
    luaPush(L, y);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int TestObject::luaNewIndex(lua_State* L) {
  auto arg = luaGet<folly::StringPiece>(L, 2);
  if (arg && *arg == "y") {
    y = luaGetChecked<int>(L, 3);
  } else {
    luaL_error(L, "Invalid field");
  }
  return 1;
}

bool TestObject::destructorCalled = false;

TEST(UserDataTest, Destruction) {
  TestObject::destructorCalled = false;
  auto L = GL.get();
  auto& obj = pushUserData<TestObject>(L, 42);
  EXPECT_EQ(42, obj.x);
  auto p = getUserData<TestObject>(L, -1);
  EXPECT_EQ(p, &obj);
  EXPECT_FALSE(TestObject::destructorCalled);
  lua_pop(L, 1);
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  EXPECT_TRUE(TestObject::destructorCalled);
}

TEST(UserDataTest, MethodsAndIndex) {
  auto L = GL.get();
  const char chunk[] =
    "return function(obj)\n"
    "    obj.y = 100\n"
    "    return obj:value(), #obj, obj.y\n"
    "end\n";
  ASSERT_EQ(0, luaL_loadstring(L, chunk));
  ASSERT_EQ(0, lua_pcall(L, 0, 1, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  pushUserData<TestObject>(L, 42);
  ASSERT_EQ(0, lua_pcall(L, 1, 3, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  EXPECT_EQ(42, luaGetChecked<int>(L, -3));
  EXPECT_EQ(10, luaGetChecked<int>(L, -2));
  EXPECT_EQ(100, luaGetChecked<int>(L, -1));
  lua_pop(L, 3);
}

// The code paths are different if we have methods, __index, or both.
// Test __index and no methods

class TestObjectIndexOnly {
 public:
  explicit TestObjectIndexOnly() : y(0) { }

  int luaIndex(lua_State* L);
  int luaNewIndex(lua_State* L);

  int y;
};

int TestObjectIndexOnly::luaIndex(lua_State* L) {
  auto arg = luaGet<folly::StringPiece>(L, 2);
  if (arg && *arg == "y") {
    luaPush(L, y);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int TestObjectIndexOnly::luaNewIndex(lua_State* L) {
  auto arg = luaGet<folly::StringPiece>(L, 2);
  if (arg && *arg == "y") {
    y = luaGetChecked<int>(L, 3);
  } else {
    luaL_error(L, "invalid field");
  }
  return 0;
}

TEST(UserDataTest, IndexOnly) {
  auto L = GL.get();
  const char chunk[] =
    "return function(obj)\n"
    "    local orig = obj.y\n"
    "    obj.y = 100\n"
    "    return orig, obj.y\n"
    "end\n";
  ASSERT_EQ(0, luaL_loadstring(L, chunk));
  ASSERT_EQ(0, lua_pcall(L, 0, 1, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  pushUserData<TestObjectIndexOnly>(L);
  ASSERT_EQ(0, lua_pcall(L, 1, 2, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  EXPECT_EQ(0, luaGetChecked<int>(L, -2));
  EXPECT_EQ(100, luaGetChecked<int>(L, -1));
  lua_pop(L, 2);
}

class TestObjectMethodsOnly {
 public:
  explicit TestObjectMethodsOnly() : y(42) { }

  int luaValue(lua_State* L);

  int y;
};

int TestObjectMethodsOnly::luaValue(lua_State* L) {
  luaPush(L, y);
  return 1;
}

TEST(UserDataTest, MethodsOnly) {
  auto L = GL.get();
  const char chunk[] =
    "return function(obj)\n"
    "    return obj:value()\n"
    "end\n";
  ASSERT_EQ(0, luaL_loadstring(L, chunk));
  ASSERT_EQ(0, lua_pcall(L, 0, 1, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  pushUserData<TestObjectMethodsOnly>(L);
  ASSERT_EQ(0, lua_pcall(L, 1, 1, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  EXPECT_EQ(42, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);
}

struct SimpleTestObject {
  static bool destructorCalled;
  int x;
  explicit SimpleTestObject(int x) : x(x) { }
  ~SimpleTestObject() {
    destructorCalled = true;
  }
};

bool SimpleTestObject::destructorCalled = false;

TEST(SimpleObjectTest, Simple) {
  auto L = GL.get();
  auto& obj = pushObject<SimpleTestObject>(L, 42);
  EXPECT_EQ(42, obj.x);

  EXPECT_EQ(&obj, getObject<SimpleTestObject>(L, -1));
  EXPECT_EQ(&obj, &getObjectChecked<SimpleTestObject>(L, -1));

  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  EXPECT_FALSE(SimpleTestObject::destructorCalled);
  lua_pop(L, 1);
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  EXPECT_TRUE(SimpleTestObject::destructorCalled);
}

}}  // namespaces

using namespace fblualib;
using namespace fblualib::test;

namespace fblualib {

template <>
const UserDataMethod<TestObject> Metatable<TestObject>::metamethods[] = {
  {"__len", &TestObject::luaLen},
  {"__index", &TestObject::luaIndex},
  {"__newindex", &TestObject::luaNewIndex},
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObject> Metatable<TestObject>::methods[] = {
  {"value", &TestObject::luaValue},
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObjectIndexOnly>
Metatable<TestObjectIndexOnly>::metamethods[] = {
  {"__index", &TestObjectIndexOnly::luaIndex},
  {"__newindex", &TestObjectIndexOnly::luaNewIndex},
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObjectIndexOnly>
Metatable<TestObjectIndexOnly>::methods[] = {
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObjectMethodsOnly>
Metatable<TestObjectMethodsOnly>::metamethods[] = {
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObjectMethodsOnly>
Metatable<TestObjectMethodsOnly>::methods[] = {
  {"value", &TestObjectMethodsOnly::luaValue},
  {nullptr, nullptr},
};

}  // namespace fblualib

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  initLuaEmbedding();
  GL = luaNewState();
  auto L = GL.get();
  luaL_openlibs(L);

  return RUN_ALL_TESTS();
}
