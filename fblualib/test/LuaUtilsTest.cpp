/**
 * Copyright 2015 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/LuaUtils.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace fblualib { namespace test {

LuaStatePtr GL;

class LuaUtilsTest : public ::testing::Test {
 public:
  void SetUp();
  void TearDown();

 protected:
  lua_State* L;

 private:
  int top_;
};

void LuaUtilsTest::SetUp() {
  L = GL.get();
  top_ = lua_gettop(L);
}

void LuaUtilsTest::TearDown() {
  lua_settop(L, top_);
}

TEST_F(LuaUtilsTest, PushBoolean) {
  luaPush(L, true);
  EXPECT_EQ(LUA_TBOOLEAN, lua_type(L, -1));
  EXPECT_TRUE(lua_toboolean(L, -1));
  EXPECT_TRUE(luaGetChecked<bool>(L, -1));

  luaPush(L, false);
  EXPECT_EQ(LUA_TBOOLEAN, lua_type(L, -1));
  EXPECT_FALSE(lua_toboolean(L, -1));
  EXPECT_FALSE(luaGetChecked<bool>(L, -1));

  lua_pushinteger(L, 1);
  EXPECT_EQ(LUA_TNUMBER, lua_type(L, -1));
  EXPECT_TRUE(lua_toboolean(L, -1));
  EXPECT_TRUE(luaGetChecked<bool>(L, -1));

  lua_pushnil(L);
  EXPECT_EQ(LUA_TNIL, lua_type(L, -1));
  EXPECT_FALSE(lua_toboolean(L, -1));
  EXPECT_FALSE(luaGetChecked<bool>(L, -1));
}

TEST_F(LuaUtilsTest, PushInteger) {
  luaPush(L, 42);
  EXPECT_EQ(42, lua_tointeger(L, -1));
  EXPECT_EQ(42, luaGetChecked<int>(L, -1));
}

TEST_F(LuaUtilsTest, PushDouble) {
  luaPush(L, 0.5);
  EXPECT_EQ(0.5, lua_tonumber(L, -1));
  EXPECT_EQ(0.5, luaGetChecked<double>(L, -1));
}

TEST_F(LuaUtilsTest, PushCString) {
  luaPush(L, "hello");
  EXPECT_STREQ("hello", lua_tostring(L, -1));
  EXPECT_STREQ("hello", luaGetChecked<const char*>(L, -1));
  EXPECT_EQ(std::string("hello"), luaGetChecked<std::string>(L, -1));
}

TEST_F(LuaUtilsTest, PushStdString) {
  std::string s("world");
  luaPush(L, s);
  EXPECT_STREQ("world", lua_tostring(L, -1));
  EXPECT_STREQ("world", luaGetChecked<const char*>(L, -1));
  EXPECT_EQ(std::string("world"), luaGetChecked<std::string>(L, -1));
}

TEST_F(LuaUtilsTest, PushTensor) {
  thpp::Tensor<double> tensor({2L, 3L});
  tensor.fill(0.5);
  luaPush(L, tensor);
  auto r = luaGetChecked<thpp::Tensor<double>>(L, -1);
  EXPECT_TRUE(tensor.isExactlyEqual(r));
}

TEST_F(LuaUtilsTest, PushStorage) {
  thpp::Storage<int> storage({10, 20, 30, 40});
  luaPush(L, storage);
  auto r = luaGetChecked<thpp::Storage<int>>(L, -1);
  EXPECT_EQ(storage.size(), r.size());
  for (size_t i = 0; i < std::min(storage.size(), r.size()); ++i) {
    EXPECT_EQ(storage.data()[i], r.data()[i]);
  }
}

TEST_F(LuaUtilsTest, TensorPtrPush) {
  ASSERT_EQ(
      0,
      luaL_loadstring(
          L,
          "require('torch')\n"
          "local t1, t2 = ...\n"
          "return t1:sum(), t2:sum()\n"));

  auto t = thpp::Tensor<float>::makePtr({4});
  t->fill(0);

  luaPush(L, t);
  t->resize({5});

  luaPush(L, *t);
  t->resize({6});

  t->fill(1);

  lua_call(L, 2, 2);
  EXPECT_EQ(6, lua_tointeger(L, -2));
  EXPECT_EQ(5, lua_tointeger(L, -1));
  lua_pop(L, 2);
}

TEST_F(LuaUtilsTest, TensorPtrGet) {
  ASSERT_EQ(
      0,
      luaL_loadstring(
          L,
          "local torch = require('torch')\n"
          "G_tensor = torch.FloatTensor(4):fill(2)\n"
          "return G_tensor\n"));
  lua_call(L, 0, 1);

  // TensorPtr operations refer to the same object
  auto t1 = luaGetChecked<thpp::Tensor<float>::Ptr>(L, -1);
  EXPECT_EQ(4, t1->size());

  t1->resize({10});
  EXPECT_EQ(10, t1->size());
  t1->fill(1);

  auto t2 = luaGetChecked<thpp::Tensor<float>::Ptr>(L, -1);
  EXPECT_EQ(10, t2->size());

  t2->resize({20});
  EXPECT_EQ(20, t1->size());
  EXPECT_EQ(20, t2->size());
  t2->fill(2);

  // Tensor (not Ptr) operations copy metadata
  auto t3 = luaGetChecked<thpp::Tensor<float>>(L, -1);
  EXPECT_EQ(20, t3.size());

  t3.resize({30});
  EXPECT_EQ(30, t3.size());
  EXPECT_EQ(20, t1->size());
  EXPECT_EQ(20, t2->size());
  t3.fill(3);

  lua_getglobal(L, "G_tensor");
  auto t4 = luaGetChecked<thpp::Tensor<float>::Ptr>(L, -1);
  EXPECT_EQ(20, t4->size());
  EXPECT_EQ(60, t4->sumall());

  lua_pushnil(L);
  lua_setglobal(L, "G_tensor");
  lua_pop(L, 2);
}

namespace {
char kRegistryKey;
}  // namespace

TEST_F(LuaUtilsTest, Registry) {
  char value;
  EXPECT_EQ(nullptr, loadPointerFromRegistry(L, &kRegistryKey));
  storePointerInRegistry(L, &kRegistryKey, &value);
  EXPECT_EQ(&value, loadPointerFromRegistry(L, &kRegistryKey));
  storePointerInRegistry(L, &kRegistryKey, nullptr);
  EXPECT_EQ(nullptr, loadPointerFromRegistry(L, &kRegistryKey));
}

TEST_F(LuaUtilsTest, Index) {
  lua_pushinteger(L, 10);
  lua_pushinteger(L, 20);
  EXPECT_EQ(10, lua_tointeger(L, -2));
  EXPECT_EQ(1, luaRealIndex(L, 1));  // positive index is left alone
  int index = luaRealIndex(L, -2);
  EXPECT_LT(0, index);
  EXPECT_EQ(10, lua_tointeger(L, index));

  // pseudo-indices are left alone
  EXPECT_EQ(LUA_REGISTRYINDEX, luaRealIndex(L, LUA_REGISTRYINDEX));
  EXPECT_EQ(LUA_GLOBALSINDEX, luaRealIndex(L, LUA_GLOBALSINDEX));
  EXPECT_EQ(lua_upvalueindex(1), luaRealIndex(L, lua_upvalueindex(1)));
  EXPECT_EQ(lua_upvalueindex(255), luaRealIndex(L, lua_upvalueindex(255)));
}

namespace {

class TestException : public std::runtime_error {
 public:
  explicit TestException(const std::string& s) : std::runtime_error(s) { }
};

template <int offset>
int testFunction(lua_State* L) {
  auto base = luaGetChecked<int>(L, lua_upvalueindex(1));
  auto a = luaGetChecked<int>(L, 1);
  auto b = luaGetChecked<int>(L, 2);
  auto sum = a + b + base + offset;
  switch (sum) {
  case 100:
    throw TestException("hit 100");
  case 101:
    luaL_error(L, "hit 101");
  }
  luaPush(L, sum);
  return 1;
}

int testWrapper(lua_State* L, lua_CFunction fn) {
  try {
    return (*fn)(L);
  } catch (const TestException& e) {
    luaL_error(L, "TestException: %s", e.what());
    return 0;  // unreached
  } catch (const std::exception& e) {
    luaL_error(L, "OTHER EXCEPTION: %s", e.what());
    return 0;  // unreached
  }
}

int testStdFunctionWrapper(lua_State* L, LuaStdFunction& fn) {
  try {
    return fn(L);
  } catch (const TestException& e) {
    luaL_error(L, "TestException: %s", e.what());
    return 0;  // unreached
  } catch (const std::exception& e) {
    luaL_error(L, "OTHER EXCEPTION: %s", e.what());
    return 0;  // unreached
  }
}

}  // namespace

TEST_F(LuaUtilsTest, WrappedCFunction) {
  int base = lua_gettop(L);
  luaPush(L, 10);
  pushWrappedCClosure(L, testFunction<0>, 1, testWrapper);
  int closureIdx = lua_gettop(L);
  EXPECT_EQ(base + 1, closureIdx);

  lua_pushvalue(L, closureIdx);
  luaPush(L, 20);
  luaPush(L, 30);
  EXPECT_EQ(0, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ(60, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);

  lua_pushvalue(L, closureIdx);
  luaPush(L, 40);
  luaPush(L, 50);
  EXPECT_EQ(LUA_ERRRUN, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ("TestException: hit 100", luaGetChecked<std::string>(L, -1));
  lua_pop(L, 1);

  lua_pushvalue(L, closureIdx);
  luaPush(L, 40);
  luaPush(L, 51);
  EXPECT_EQ(LUA_ERRRUN, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ("hit 101", luaGetChecked<std::string>(L, -1));
  lua_pop(L, 1);
}

TEST_F(LuaUtilsTest, StdFunction) {
  int foo = 100;
  int base = lua_gettop(L);
  luaPush(L, 10);
  pushStdFunction(L,
      [foo] (lua_State* L) {
        int a = foo;
        a += luaGetChecked<int>(L, 1);
        a += luaGetChecked<int>(L, lua_upvalueindex(1));
        luaPush(L, a);
        return 1;
      },
      1);
  int closureIdx = lua_gettop(L);
  EXPECT_EQ(base + 1, closureIdx);

  lua_pushvalue(L, closureIdx);
  luaPush(L, 20);
  EXPECT_EQ(0, lua_pcall(L, 1, 1, 0));
  EXPECT_EQ(130, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);
}

TEST_F(LuaUtilsTest, WrappedStdFunction) {
  int foo = 100;
  int base = lua_gettop(L);
  luaPush(L, 10);
  pushWrappedStdFunction(L,
      [foo] (lua_State* L) {
        int a = foo;
        a += luaGetChecked<int>(L, 1);
        a += luaGetChecked<int>(L, lua_upvalueindex(1));
        if (a == 100) {
          throw TestException("hit 100");
        }
        luaPush(L, a);
        return 1;
      },
      1,
      testStdFunctionWrapper);
  int closureIdx = lua_gettop(L);
  EXPECT_EQ(base + 1, closureIdx);

  lua_pushvalue(L, closureIdx);
  luaPush(L, 20);
  EXPECT_EQ(0, lua_pcall(L, 1, 1, 0));
  EXPECT_EQ(130, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);

  lua_pushvalue(L, closureIdx);
  luaPush(L, -10);
  EXPECT_EQ(LUA_ERRRUN, lua_pcall(L, 1, 1, 0));
  EXPECT_EQ("TestException: hit 100", luaGetChecked<std::string>(L, -1));
}

namespace {

const luaL_Reg kFuncs[] = {
  {"add", &testFunction<0>},
  {"add1", &testFunction<1>},
  {nullptr, nullptr},
};

}  // namespace

TEST_F(LuaUtilsTest, SetWrappedFuncs) {
  int base = lua_gettop(L);
  lua_newtable(L);
  luaPush(L, 10);
  setWrappedFuncs(L, kFuncs, 1, testWrapper);
  int tableIdx = lua_gettop(L);
  EXPECT_EQ(base + 1, tableIdx);

  lua_getfield(L, tableIdx, "add");
  luaPush(L, 20);
  luaPush(L, 30);
  EXPECT_EQ(0, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ(60, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, tableIdx, "add");
  luaPush(L, 40);
  luaPush(L, 50);
  EXPECT_EQ(LUA_ERRRUN, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ("TestException: hit 100", luaGetChecked<std::string>(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, tableIdx, "add");
  luaPush(L, 40);
  luaPush(L, 51);
  EXPECT_EQ(LUA_ERRRUN, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ("hit 101", luaGetChecked<std::string>(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, tableIdx, "add1");
  luaPush(L, 20);
  luaPush(L, 30);
  EXPECT_EQ(0, lua_pcall(L, 2, 1, 0));
  EXPECT_EQ(61, luaGetChecked<int>(L, -1));
  lua_pop(L, 1);
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
  lua_getglobal(L, "require");
  lua_pushstring(L, "torch");
  lua_call(L, 1, 0);

  return RUN_ALL_TESTS();
}
