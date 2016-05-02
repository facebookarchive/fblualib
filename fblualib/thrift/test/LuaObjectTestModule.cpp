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
  auto p = luaGetTensorChecked<double>(L, 1);
  return pushAsString(L, make(*p));
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

int checkTableIteration(lua_State* L) {
  auto obj = getFromString(L, 1);

  // clowny
  std::vector<folly::Optional<double>> listVals { 10, 20, 30 };
  folly::Optional<double> trueVal = 40;
  folly::Optional<double> falseVal = 50;
  std::unordered_map<std::string, folly::Optional<double>> stringVals {
    {"hello", 60},
    {"world", 70},
  };
  std::unordered_map<int, folly::Optional<double>> intVals {
    {100, 80},
    {200, 90},
  };

  auto begin = tableBegin(obj);
  auto end = tableEnd(obj);
  for (auto it = begin; it != end; ++it) {
    auto& key = it->first;
    auto& value = it->second;

    double ev = 0;

    switch (getType(key, obj.refs)) {
    case LuaObjectType::NIL:
      luaL_error(L, "invalid NIL key");
      break;
    case LuaObjectType::DOUBLE:
      {
        auto dk = getDouble(key);
        auto ik = int(dk);
        if (double(ik) != dk) {
          luaL_error(L, "invalid non-int numeric key");
        }
        if (ik >= 1 && ik <= listVals.size()) {
          auto& evp = listVals[ik - 1];
          if (!evp) {
            luaL_error(L, "duplicate list key");
          }
          ev = *evp;
          evp.clear();
        } else {
          auto pos = intVals.find(ik);
          if (pos == intVals.end()) {
            luaL_error(L, "unrecognized int key");
          }
          if (!pos->second) {
            luaL_error(L, "duplicate int key");
          }
          ev = *pos->second;
          pos->second.clear();
        }
      }
      break;
    case LuaObjectType::BOOL:
      {
        auto bk = getBool(key);
        if (bk) {
          if (!trueVal) {
            luaL_error(L, "duplicate true key");
          }
          ev = *trueVal;
          trueVal.clear();
        } else {
          if (!falseVal) {
            luaL_error(L, "duplicate false key");
          }
          ev = *falseVal;
          falseVal.clear();
        }
      }
      break;
    case LuaObjectType::STRING:
      {
        auto sk = getString(key, obj.refs).str();
        auto pos = stringVals.find(sk);
        if (pos == stringVals.end()) {
          luaL_error(L, "unrecognized string key");
        }
        if (!pos->second) {
          luaL_error(L, "duplicate string key");
        }
        ev = *pos->second;
        pos->second.clear();
      }
      break;
    default:
      luaL_error(L, "invalid key type");
    }

    double fv = getDouble(value);
    if (ev != fv) {
      luaL_error(L, "mismatched value");
    }
  }

  for (auto& v : listVals) {
    if (v) {
      luaL_error(L, "list key missing");
    }
  }

  if (trueVal) {
    luaL_error(L, "true key missing");
  }

  if (falseVal) {
    luaL_error(L, "false key missing");
  }

  for (auto& p : stringVals) {
    if (p.second) {
      luaL_error(L, "string key missing");
    }
  }

  for (auto& p : intVals) {
    if (p.second) {
      luaL_error(L, "int key missing");
    }
  }

  return 0;
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
  {"check_table_iteration", checkTableIteration},
  {nullptr, nullptr},  // sentinel
};

}  // namespace

extern "C" int LUAOPEN(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, nullptr, gFuncs);
  return 1;
}
