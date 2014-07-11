/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/LuaUtils.h>
#include <fblualib/thrift/LuaObject.h>

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

const struct luaL_reg gFuncs[] = {
  {"write_nil", writeNil},
  {"write_bool", writeBool},
  {"write_double", writeDouble},
  {"write_string", writeString},
  {"write_tensor", writeTensor},
  {"read_nil", readNil},
  {"read_bool", readBool},
  {"read_double", readDouble},
  {"read_string", readString},
  {"read_tensor", readTensor},
  {nullptr, nullptr},  // sentinel
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, gFuncs);
  return 1;
}
