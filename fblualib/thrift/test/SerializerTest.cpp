/*
 * Copyright 2016 Facebook, Inc.
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

#include <fblualib/LuaUtils.h>
#include <fblualib/thrift/Serialization.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace fblualib { namespace thrift { namespace test {

namespace {

const char* kIOBMetatableKey = "thrift.test.iobmetatable";

void createTestObject(lua_State* L, const char* mtKey) {
  lua_newtable(L);

  lua_pushinteger(L, 42);
  lua_rawseti(L, -2, 1);

  lua_newtable(L);
  lua_pushstring(L, "hello");
  lua_rawseti(L, -2, 1);

  lua_pushvalue(L, -1);
  lua_rawseti(L, -3, 2);
  lua_rawseti(L, -2, 3);

  luaPush(L, thpp::Tensor<float>({2, 2}));
  lua_rawseti(L, -2, 4);

  int val = 100;
  auto ptr = lua_newuserdata(L, sizeof(val));
  luaL_getmetatable(L, mtKey);
  lua_setmetatable(L, -2);
  memcpy(ptr, &val, sizeof(val));
  lua_rawseti(L, -2, 5);

  val = 200;
  ptr = lua_newuserdata(L, sizeof(val));
  luaL_getmetatable(L, mtKey);
  lua_setmetatable(L, -2);
  memcpy(ptr, &val, sizeof(val));
  lua_rawseti(L, -2, 6);
}

void checkTestObject(lua_State* L, const char* mtKey) {
  lua_rawgeti(L, -1, 1);
  EXPECT_EQ(42, lua_tointeger(L, -1));
  lua_pop(L, 1);

  lua_rawgeti(L, -1, 2);
  lua_rawgeti(L, -2, 3);
  EXPECT_TRUE(lua_rawequal(L, -1, -2));
  lua_pop(L, 1);
  lua_rawgeti(L, -1, 1);
  EXPECT_STREQ("hello", lua_tostring(L, -1));
  lua_pop(L, 2);

  lua_rawgeti(L, -1, 4);
  auto t = luaGetChecked<thpp::Tensor<float>::Ptr>(L, -1);
  lua_pop(L, 1);

  lua_rawgeti(L, -1, 5);
  luaL_checkudata(L, -1, mtKey);
  auto ptr = reinterpret_cast<const int*>(lua_topointer(L, -1));
  EXPECT_EQ(100, *ptr);
  lua_pop(L, 1);

  lua_rawgeti(L, -1, 6);
  luaL_checkudata(L, -1, mtKey);
  ptr = reinterpret_cast<const int*>(lua_topointer(L, -1));
  EXPECT_EQ(200, *ptr);
  lua_pop(L, 1);

  lua_pop(L, 1);
}

TEST(IOBufSerializerTest, Simple) {
  auto luaState = luaNewState();
  auto L = luaState.get();

  luaL_openlibs(L);

  lua_getglobal(L, "require");
  lua_pushstring(L, "torch");
  lua_call(L, 1, 0);

  lua_getglobal(L, "require");
  lua_pushstring(L, "fb.thrift");
  lua_call(L, 1, 0);

  luaL_newmetatable(L, kIOBMetatableKey);
  registerUserDataCallbacks(
      L,
      "thrift.test",
      -1,
      [this] (lua_State* L, int index) {
        auto p = luaL_checkudata(L, index, kIOBMetatableKey);
        folly::IOBuf buf(folly::IOBuf::CREATE, sizeof(int));
        folly::io::Appender appender(&buf, 0);
        appender.push(static_cast<const uint8_t*>(p), sizeof(int));
        return buf;
      },
      [this] (lua_State* L, const folly::IOBuf& buf) {
        auto p = lua_newuserdata(L, sizeof(int));
        luaL_getmetatable(L, kIOBMetatableKey);
        lua_setmetatable(L, -2);

        folly::io::Cursor cursor(&buf);
        cursor.pull(p, sizeof(int));
      });

  createTestObject(L, kIOBMetatableKey);
  checkTestObject(L, kIOBMetatableKey);
}

class SerializerTest : public ::testing::TestWithParam<int> {
 public:
  SerializerTest();
  void SetUp();

 protected:
  LuaStatePtr L1p_;
  LuaStatePtr L2p_;
  lua_State* L1 = nullptr;
  lua_State* L2 = nullptr;

  bool localMode_ = false;
  bool localModeFallThrough_ = false;
  bool makePortable_ = false;

  void runTest();
  LuaStatePtr initLuaState();

  std::unique_ptr<MemUserData> makeMemUserData(lua_State* L, int index);
  std::unique_ptr<MemUserData> deserializeMemUserData(const folly::IOBuf& buf);
};

INSTANTIATE_TEST_CASE_P(All, SerializerTest, ::testing::Range(0, 8));

SerializerTest::SerializerTest() { }

const char* kMetatableKey = "thrift.test.metatable";

LuaStatePtr SerializerTest::initLuaState() {
  auto luaState = luaNewState();
  auto L = luaState.get();
  luaL_openlibs(L);

  lua_getglobal(L, "require");
  lua_pushstring(L, "torch");
  lua_call(L, 1, 0);

  lua_getglobal(L, "require");
  lua_pushstring(L, "fb.thrift");
  lua_call(L, 1, 0);

  luaL_newmetatable(L, kMetatableKey);
  registerUserDataCallbacks(
      L,
      "thrift.test",
      -1,
      [this] (lua_State* L, int index) {
        return makeMemUserData(L, index);
      },
      [this] (const folly::IOBuf& buf) {
        return deserializeMemUserData(buf);
      });

  return luaState;
}

void SerializerTest::SetUp() {
  L1p_ = initLuaState();
  L2p_ = initLuaState();
  L1 = L1p_.get();
  L2 = L2p_.get();

  auto p = GetParam();
  localMode_ = p & 1;
  localModeFallThrough_ = p & 2;
  makePortable_ = p & 3;
}

class IntMemUserData : public MemUserData {
 public:
  explicit IntMemUserData(int val) : val_(val) { }

 private:
  void doLuaPush(lua_State* L) override;
  folly::IOBuf doSerialize(const SerializerOptions& options) const override;
  folly::StringPiece doKey() const override { return "thrift.test"; }

  int val_;
};

void IntMemUserData::doLuaPush(lua_State* L) {
  auto ptr = lua_newuserdata(L, sizeof(val_));
  luaL_getmetatable(L, kMetatableKey);
  lua_setmetatable(L, -2);
  memcpy(ptr, &val_, sizeof(val_));
}

folly::IOBuf IntMemUserData::doSerialize(
    const SerializerOptions& options) const {
  folly::IOBuf buf(folly::IOBuf::CREATE, sizeof(int));
  folly::io::Appender appender(&buf, 0);
  appender.writeBE(val_);
  return buf;
}

std::unique_ptr<MemUserData> SerializerTest::makeMemUserData(
    lua_State* L, int objIndex) {
  luaL_checkudata(L, objIndex, kMetatableKey);
  return std::make_unique<IntMemUserData>(
      *static_cast<const int*>(lua_topointer(L, objIndex)));
}

std::unique_ptr<MemUserData> SerializerTest::deserializeMemUserData(
    const folly::IOBuf& buf) {
  auto cursor = folly::io::Cursor(&buf);
  return std::make_unique<IntMemUserData>(cursor.readBE<int>());
}

TEST_P(SerializerTest, Dummy) {
  createTestObject(L1, kMetatableKey);
  checkTestObject(L1, kMetatableKey);
}

TEST_P(SerializerTest, CrossInterpreter) {
  Serializer::Options options;
  options.localMode = localMode_;

  Serializer serializer(L1, std::move(options));

  createTestObject(L1, kMetatableKey);
  auto obj = serializer.serialize(-1);
  auto p = serializer.finishLocal();
  Deserializer deserializer(L2);

  if (makePortable_) {
    deserializer.start(&p.makePortable());
  } else {
    deserializer.start(&p);
  }

  EXPECT_EQ(1, deserializer.deserialize(obj));
  checkTestObject(L2, kMetatableKey);
}

}  // namespace

}}}  // namespaces

using namespace fblualib;
using namespace fblualib::thrift::test;

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  initLuaEmbedding();

  return RUN_ALL_TESTS();
}
