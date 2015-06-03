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
}

}}  // namespaces

using namespace fblualib;
using namespace fblualib::test;

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  initLuaEmbedding();

  GL = luaNewState();
  auto L = GL.get();
  luaL_openlibs(L);
  lua_getglobal(L, "require");
  lua_pushstring(L, "torch");
  lua_call(L, 1, 0);

  return RUN_ALL_TESTS();
}
