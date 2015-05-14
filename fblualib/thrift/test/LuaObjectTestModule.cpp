/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/LuaUtils.h>
#include <fblualib/thrift/LuaObject.h>
#include <fblualib/thrift/Serialization.h>

using namespace fblualib;
using namespace fblualib::thrift;

namespace {

int pushAsString(lua_State* L, const LuaObject& obj) {
  StringWriter writer;
  cppEncode(obj, folly::io::CodecType::LZ4, writer);

  auto str = folly::StringPiece(writer.finish());
  lua_pushlstring(L, str.data(), str.size());
  return 1;
}

int writeNil(lua_State* L) {
  return pushAsString(L, make());
}

int writeBool(lua_State* L) {
  return pushAsString(L, make(bool(lua_toboolean(L, 1))));
}

int writeDouble(lua_State* L) {
  return pushAsString(L, make(lua_tonumber(L, 1)));
}

int writeString(lua_State* L) {
  return pushAsString(L, make(luaGetStringChecked(L, 1)));
}

int writeTensor(lua_State* L) {
  return pushAsString(L, make(luaGetTensorChecked<double>(L, 1)));
}

LuaObject getFromString(lua_State* L, int index) {
  folly::ByteRange br(luaGetStringChecked(L, 1));
  StringReader reader(&br);
  return cppDecode(reader);
}

int readNil(lua_State* L) {
  if (!isNil(getFromString(L, 1))) {
    luaL_error(L, "not nil");
  }
  return 0;
}

int readBool(lua_State* L) {
  lua_pushboolean(L, getBool(getFromString(L, 1)));
  return 1;
}

int readDouble(lua_State* L) {
  lua_pushnumber(L, getDouble(getFromString(L, 1)));
  return 1;
}

int readString(lua_State* L) {
  auto obj = getFromString(L, 1);
  auto sp = getString(obj);
  lua_pushlstring(L, sp.data(), sp.size());
  return 1;
}

int readTensor(lua_State* L) {
  luaPushTensor(L, getTensor<double>(getFromString(L, 1)));
  return 1;
}

const char* kMetatableKey = "thrift.test.metatable";

int doPushUserData(lua_State* L, int val) {
  auto ptr = lua_newuserdata(L, sizeof(val));
  luaL_getmetatable(L, kMetatableKey);
  lua_setmetatable(L, -2);
  memcpy(ptr, &val, sizeof(val));
  return 1;
}

int newUserData(lua_State* L) {
  int val = luaL_checkint(L, 1);
  return doPushUserData(L, val);
}

int getUserData(lua_State* L) {
  luaL_checkudata(L, 1, kMetatableKey);
  auto ptr = reinterpret_cast<const int*>(lua_topointer(L, 1));
  lua_pushinteger(L, *ptr);
  return 1;
}

folly::IOBuf wrap(const char* ptr) {
  return folly::IOBuf(folly::IOBuf::WRAP_BUFFER, ptr, strlen(ptr));
}

folly::IOBuf customDataSerializer(lua_State* L, int objIndex) {
  auto ptr = lua_topointer(L, objIndex);
  luaL_getmetatable(L, kMetatableKey);
  lua_getmetatable(L, objIndex);
  CHECK(lua_rawequal(L, -1, -2));
  lua_pop(L, 2);
  return folly::IOBuf(folly::IOBuf::COPY_BUFFER, ptr, sizeof(int));
}

void customDataDeserializer(lua_State* L, const folly::IOBuf& buf) {
  folly::io::Cursor cursor(&buf);
  doPushUserData(L, cursor.read<int>());
}

const struct luaL_reg gFuncs[] = {
  // write_ functions return a string representing the Thrift-encoded argument
  {"write_nil", writeNil},
  {"write_bool", writeBool},
  {"write_double", writeDouble},
  {"write_string", writeString},
  {"write_tensor", writeTensor},
  // read_ functions read a string representing a Thrift-encoded value,
  // decode it, check that the type matches, and return the decoded value
  {"read_nil", readNil},
  {"read_bool", readBool},
  {"read_double", readDouble},
  {"read_string", readString},
  {"read_tensor", readTensor},
  // Create a new userdata object that encapsulates an int
  {"new_userdata", newUserData},
  // Get the encapsulated int from the given userdata object, checking the type
  {"get_userdata", getUserData},
  {nullptr, nullptr},  // sentinel
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, gFuncs);
  luaL_newmetatable(L, kMetatableKey);
  registerUserDataCallbacks(
      L,
      "thrift.test",
      -1,
      customDataSerializer,
      customDataDeserializer);
  lua_pop(L, 1);
  return 1;
}
