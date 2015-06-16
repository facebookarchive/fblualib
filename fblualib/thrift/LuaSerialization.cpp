/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <lua.hpp>
#include <fblualib/LuaUtils.h>
#include "Encoding.h"
#include "Serialization.h"
#include <folly/io/Compression.h>

using namespace fblualib;
using namespace fblualib::thrift;
using folly::io::CodecType;

namespace {

struct CodecInfo {
  const char* name;
  CodecType type;
};

CodecInfo gCodecs[] = {
  {"NONE", CodecType::NO_COMPRESSION},
  {"LZ4", CodecType::LZ4},
  {"SNAPPY", CodecType::SNAPPY},
  {"ZLIB", CodecType::ZLIB},
  {"LZMA2", CodecType::LZMA2},
};

constexpr size_t kCodecCount = sizeof(gCodecs) / sizeof(gCodecs[0]);

LuaVersionInfo getVersion(lua_State* L) {
  int origTop = lua_gettop(L);
  lua_getglobal(L, "jit");
  if (lua_isnil(L, -1)) {
    luaL_error(L, "Cannot find global \"jit\", cannot determine version");
  }
  int jitIdx = lua_gettop(L);

  // Just to make sure, look at jit.version, see if it starts with LuaJIT
  lua_getfield(L, jitIdx, "version");
  auto ver = luaGetStringChecked(L, -1);
  if (!ver.startsWith("LuaJIT")) {
    luaL_error(L, "Invalid jit.version, expecting LuaJIT: %*s",
               ver.size(), ver.data());
  }

  LuaVersionInfo info;
  info.interpreterVersion = ver.str();

  // LuaJIT bytecode is compatible between the same <major>.<minor>
  // version_num is <major> * 10000 + <minor> * 100 + <patchlevel>
  lua_getfield(L, jitIdx, "version_num");
  long verNum = lua_tointeger(L, -1);
  if (verNum < 2 * 100 * 100) {
    luaL_error(L, "Invalid LuaJIT version, expected >= 20000: %ld", verNum);
  }

  info.bytecodeVersion = folly::sformat("LuaJIT:{:04d}", verNum / 100);

  lua_settop(L, origTop);
  return info;
}

int serializeToString(lua_State* L) {
  auto codecType =
    (lua_type(L, 2) != LUA_TNIL && lua_type(L, 2) != LUA_TNONE ?
     static_cast<CodecType>(luaL_checkinteger(L, 2)) :
     CodecType::NO_COMPRESSION);
  auto luaChunkSize = luaGetNumber<uint64_t>(L, 4);
  uint64_t chunkSize =
    luaChunkSize ? *luaChunkSize : std::numeric_limits<uint64_t>::max();

  auto obj = Serializer::toThrift(L, 1, 3);

  StringWriter writer;
  encode(obj, codecType, getVersion(L), writer, kAnyVersion, chunkSize);

  auto str = folly::StringPiece(writer.finish());
  lua_pushlstring(L, str.data(), str.size());
  return 1;
}

int serializeToFile(lua_State* L) {
  auto codecType =
    (lua_type(L, 3) != LUA_TNIL && lua_type(L, 3) != LUA_TNONE ?
     static_cast<CodecType>(luaL_checkinteger(L, 3)) :
     CodecType::NO_COMPRESSION);
  auto luaChunkSize = luaGetNumber<uint64_t>(L, 5);
  uint64_t chunkSize =
    luaChunkSize ? *luaChunkSize : std::numeric_limits<uint64_t>::max();

  auto fp = luaDecodeFILE(L, 2);

  auto obj = Serializer::toThrift(L, 1, 4);

  FILEWriter writer(fp);
  encode(obj, codecType, getVersion(L),  writer, kAnyVersion, chunkSize);

  return 0;
}

int doDeserialize(lua_State* L, DecodedObject&& decodedObject, int envIdx) {
  auto version = getVersion(L);

  unsigned int options = 0;
  // Check for bytecode version compatibility
  auto& decodedBytecodeVersion = decodedObject.luaVersionInfo.bytecodeVersion;
  if (decodedBytecodeVersion.empty() ||
      decodedBytecodeVersion != version.bytecodeVersion) {
    options |= Deserializer::NO_BYTECODE;
  }

  return Deserializer::fromThrift(L, std::move(decodedObject.output),
                                  envIdx, options);
}

int deserializeFromString(lua_State* L) {
  folly::ByteRange br(luaGetStringChecked(L, 1));
  StringReader reader(&br);
  return doDeserialize(L, decode(reader), 2);
}

int deserializeFromFile(lua_State* L) {
  auto fp = luaDecodeFILE(L, 1);
  FILEReader reader(fp);
  return doDeserialize(L, decode(reader), 2);
}

int setCallbacks(lua_State* L) {
  // Set serialization and deserialization callbacks for special objects
  luaL_checktype(L, 1, LUA_TFUNCTION);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  setSpecialSerializationCallback(L, 1);
  setSpecialDeserializationCallback(L, 2);
  return 0;
}

const struct luaL_reg gFuncs[] = {
  {"_to_string", serializeToString},
  {"_to_file", serializeToFile},
  {"_from_string", deserializeFromString},
  {"_from_file", deserializeFromFile},
  {"_set_callbacks", setCallbacks},
  {nullptr, nullptr},  // sentinel
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, gFuncs);

  // Create "codec" table
  lua_newtable(L);

  for (size_t i = 0; i < kCodecCount; ++i) {
    auto codecType = gCodecs[i].type;
    try {
      auto codec = folly::io::getCodec(codecType);
    } catch (const std::invalid_argument& e) {
      continue;
    }
    lua_pushinteger(L, static_cast<int>(codecType));
    lua_setfield(L, -2, gCodecs[i].name);
  }
  lua_setfield(L, -2, "codec");

  return 1;
}
