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
  static bool gcCalled;
  static bool destructorCalled;

  explicit TestObject(int x) : x(x), y(0) { }
  virtual ~TestObject() { destructorCalled = true; }

  int luaLen(lua_State* L);
  int luaValue(lua_State* L);
  int luaIndex(lua_State* L);
  int luaNewIndex(lua_State* L);
  int luaGC(lua_State* L);
  virtual int luaFoo(lua_State* L);

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

int TestObject::luaFoo(lua_State* L) {
  lua_pushstring(L, "base");
  return 1;
}

int TestObject::luaGC(lua_State* L) {
  gcCalled = true;
  return 0;
}

bool TestObject::destructorCalled = false;
bool TestObject::gcCalled = false;

TEST(UserDataTest, Destruction) {
  TestObject::destructorCalled = false;
  TestObject::gcCalled = false;
  auto L = GL.get();
  auto& obj = pushUserData<TestObject>(L, 42);
  EXPECT_EQ(42, obj.x);
  auto p = getUserData<TestObject>(L, -1);
  EXPECT_EQ(&obj, p);
  EXPECT_FALSE(TestObject::gcCalled);
  EXPECT_FALSE(TestObject::destructorCalled);
  lua_pop(L, 1);
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  EXPECT_TRUE(TestObject::gcCalled);
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

// The code paths are different if we have __index or not.

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

namespace {

class TestDerived1 : public TestObject {
 public:
  static bool derivedDestructorCalled;

  TestDerived1(int xv, int zv) : TestObject(xv), z(zv) { }

  ~TestDerived1() override {
    derivedDestructorCalled = true;
  }

  int luaFoo(lua_State* L) override;
  int luaBar(lua_State* L);
  int luaValue(lua_State* L);

  int z;
};

int TestDerived1::luaBar(lua_State* L) {
  lua_pushstring(L, "bar");
  return 1;
}

int TestDerived1::luaFoo(lua_State* L) {
  lua_pushstring(L, "derived");
  return 1;
}

int TestDerived1::luaValue(lua_State* L) {
  lua_pushinteger(L, z);
  return 1;
}

bool TestDerived1::derivedDestructorCalled = false;

}  // namespace

}}  // namespaces

using namespace fblualib;
using namespace fblualib::test;

namespace fblualib {

template <> struct BaseClass<TestDerived1> {
  typedef TestObject type;
};

}  // namespace fblualib

namespace fblualib { namespace test {

TEST(InheritanceTest, Destruction) {
  TestObject::destructorCalled = false;
  TestDerived1::destructorCalled = false;
  auto L = GL.get();
  auto& obj = pushUserData<TestDerived1>(L, 10, 20);
  EXPECT_EQ(10, obj.x);
  EXPECT_EQ(20, obj.z);
  auto p = getUserData<TestDerived1>(L, -1);
  EXPECT_EQ(&obj, p);
  auto q = getUserData<TestObject>(L, -1);
  EXPECT_EQ(static_cast<TestObject*>(&obj), q);
  lua_pop(L, 1);
  lua_gc(L, LUA_GCCOLLECT, 0);
  lua_gc(L, LUA_GCCOLLECT, 0);
  EXPECT_TRUE(TestDerived1::destructorCalled);
  EXPECT_TRUE(TestObject::destructorCalled);
}

}}  // namespaces

namespace fblualib {

TEST(InheritanceTest, Methods) {
  auto L = GL.get();

  const char chunk[] =
    "return function(obj)\n"
    "  return #obj, obj:value(), obj:foo(), obj:bar()\n"
    "end\n";

  ASSERT_EQ(0, luaL_loadstring(L, chunk));
  ASSERT_EQ(0, lua_pcall(L, 0, 1, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);
  pushUserData<TestDerived1>(L, 100, 200);
  ASSERT_EQ(0, lua_pcall(L, 1, 4, 0))
    << luaGetChecked<folly::StringPiece>(L, -1);

  EXPECT_EQ(10, luaGetChecked<int>(L, -4));
  EXPECT_EQ(100, luaGetChecked<int>(L, -3));  // calls base method!
  EXPECT_EQ("derived", luaGetChecked<std::string>(L, -2));  // virtual
  EXPECT_EQ("bar", luaGetChecked<std::string>(L, -1));
}

template <>
const UserDataMethod<TestObject> Metatable<TestObject>::methods[] = {
  {"__len", &TestObject::luaLen},
  {"__index", &TestObject::luaIndex},
  {"__newindex", &TestObject::luaNewIndex},
  {"__gc", &TestObject::luaGC},
  {"foo", &TestObject::luaFoo},
  {"value", &TestObject::luaValue},
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestObjectMethodsOnly>
Metatable<TestObjectMethodsOnly>::methods[] = {
  {"value", &TestObjectMethodsOnly::luaValue},
  {nullptr, nullptr},
};

template <>
const UserDataMethod<TestDerived1> Metatable<TestDerived1>::methods[] = {
  {"bar", &TestDerived1::luaBar},
  {nullptr, nullptr},
};

}  // namespace fblualib

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  initLuaEmbedding();
  GL = luaNewState();
  auto L = GL.get();
  luaL_openlibs(L);

  return RUN_ALL_TESTS();
}
