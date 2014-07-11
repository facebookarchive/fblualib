/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "Serialization.h"
#include <fblualib/LuaUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace fblualib { namespace thrift {

namespace {

const char* kSpecialSerializationCallbackKey =
  "thrift.special_callback.serialization";
const char* kSpecialDeserializationCallbackKey =
  "thrift.special_callback.deserialization";

}  // namespace

void setSpecialSerializationCallback(lua_State* L, int index) {
  lua_pushvalue(L, index);
  lua_setfield(L, LUA_REGISTRYINDEX, kSpecialSerializationCallbackKey);
}

void setSpecialDeserializationCallback(lua_State* L, int index) {
  lua_pushvalue(L, index);
  lua_setfield(L, LUA_REGISTRYINDEX, kSpecialDeserializationCallbackKey);
}

LuaObject Serializer::toThrift(lua_State* L, int index) {
  doSerialize(out_.value, L, index);
  converted_.clear();
  return std::move(out_);
}

void Serializer::doSerialize(LuaPrimitiveObject& obj, lua_State* L, int index) {
  if (index < 0) {
    index = lua_gettop(L) + index + 1;
  }

  LuaRefObject ref;
  int64_t refIdx = -1;
  // Check if we've encountered it before, record if not
  if (auto luaPtr = lua_topointer(L, index)) {
    auto pos = converted_.find(luaPtr);
    if (pos != converted_.end()) {
      obj.refVal = pos->second;
      obj.__isset.refVal = true;
      return;
    }
    refIdx = out_.refs.size();
    converted_[luaPtr] = refIdx;
    obj.__isset.refVal = true;
    obj.refVal = refIdx;
    out_.refs.emplace_back();
  }

  int type = lua_type(L, index);
  switch (type) {
  case LUA_TNIL:
    DCHECK_EQ(refIdx, -1);
    obj.isNil = true;
    break;  // we're done!
  case LUA_TNUMBER:
    DCHECK_EQ(refIdx, -1);
    obj.doubleVal = lua_tonumber(L, index);
    obj.__isset.doubleVal = true;
    break;
  case LUA_TBOOLEAN:
    DCHECK_EQ(refIdx, -1);
    obj.boolVal = lua_toboolean(L, index);
    obj.__isset.boolVal = true;
    break;
  case LUA_TSTRING: {
    size_t len;
    const char* data = lua_tolstring(L, index, &len);
    // Strings may be references or not, depending on whether they're interned
    if (refIdx == -1) {
      obj.stringVal.assign(data, len);
      obj.__isset.stringVal = true;
    } else {
      ref.stringVal.assign(data, len);
      ref.__isset.stringVal = true;
    }
    break;
  }
  case LUA_TTABLE:
    DCHECK_GE(refIdx, 0);
    ref.__isset.tableVal = true;
    doSerializeTable(ref.tableVal, L, index);
    break;
  case LUA_TUSERDATA:
    DCHECK_GE(refIdx, 0);
#define SERIALIZE_TENSOR(TYPE) \
    { \
      auto tensor = luaGetTensor<TYPE>(L, index); \
      if (tensor) { \
        ref.__isset.tensorVal = true; \
        tensor->serialize(ref.tensorVal); \
        break; \
      } \
    }
    SERIALIZE_TENSOR(unsigned char)
    SERIALIZE_TENSOR(int32_t)
    SERIALIZE_TENSOR(int64_t)
    SERIALIZE_TENSOR(float)
    SERIALIZE_TENSOR(double)
#undef SERIALIZE_TENSOR

#define SERIALIZE_STORAGE(TYPE) \
    { \
      auto storage = luaGetStorage<TYPE>(L, index); \
      if (storage) { \
        ref.__isset.storageVal = true; \
        storage->serialize(ref.storageVal); \
        break; \
      } \
    }
    SERIALIZE_STORAGE(unsigned char)
    SERIALIZE_STORAGE(int32_t)
    SERIALIZE_STORAGE(int64_t)
    SERIALIZE_STORAGE(float)
    SERIALIZE_STORAGE(double)
#undef SERIALIZE_STORAGE
    luaL_error(L, "invalid userdata");
    break;
  case LUA_TFUNCTION:
    DCHECK_GE(refIdx, 0);
    ref.__isset.functionVal = true;
    doSerializeFunction(ref.functionVal, L, index);
    break;
  default:
    luaL_error(L, "invalid type %d", type);
  }

  if (refIdx >= 0) {
    out_.refs[refIdx] = std::move(ref);
  }
}

void Serializer::doSerializeTable(LuaTable& obj,
                                  lua_State* L, int index) {
  int top = lua_gettop(L);

  int n = lua_getmetatable(L, index);
  if (n) {
    // This table has a metatable. Check the special callback first, to
    // see if we need to do anything interesting with it.
    int metatableIdx = lua_gettop(L);

    lua_getfield(L, LUA_REGISTRYINDEX, kSpecialSerializationCallbackKey);
    if (!lua_isnil(L, -1)) {
      // We have a special callback. Call it:
      //
      // key, table, metatable = callback(table)
      lua_pushvalue(L, index);  // table
      lua_call(L, 1, 4);
      int retMetatableIdx = lua_gettop(L);
      int retTableIdx = retMetatableIdx - 1;
      int retValIdx = retMetatableIdx - 2;
      int retKeyIdx = retMetatableIdx - 3;

      if (!lua_isnil(L, retKeyIdx)) {
        obj.__isset.specialKey = true;
        doSerialize(obj.specialKey, L, retKeyIdx);
      }

      if (!lua_isnil(L, retValIdx)) {
        obj.__isset.specialValue = true;
        doSerialize(obj.specialValue, L, retValIdx);
      }

      // nil = serialize current metatable
      // false = serialize no metatable
      if (!lua_isnil(L, retMetatableIdx)) {
        if (!lua_toboolean(L, retMetatableIdx)) {  // check if false
          metatableIdx = 0;
        } else {
          metatableIdx = retMetatableIdx;
        }
      }
      if (!lua_isnil(L, retTableIdx)) {
        index = retTableIdx;
      }
    }
    if (metatableIdx) {
      obj.__isset.metatable = true;
      doSerialize(obj.metatable, L, metatableIdx);
    }
  }

  // Get list-like elements (consecutive integers, starting at 1)
  auto listSize = lua_objlen(L, index);
  if (listSize > 0) {
    obj.__isset.listKeys = true;
    obj.listKeys.reserve(listSize);
    for (int i = 1; i <= listSize; ++i) {
      lua_rawgeti(L, index, i);
      if (lua_isnil(L, -1)) {
        // lua_objlen will return an integer n such that table[n] exists
        // but table[n+1] doesn't; not necessarily the smallest such n.
        // So we check.
        lua_pop(L, 1);
        break;
      }
      obj.listKeys.emplace_back();
      doSerialize(obj.listKeys.back(), L, -1);
      lua_pop(L, 1);
    }
  }

  // Iterate through other elements in the table
  lua_pushnil(L);
  while (lua_next(L, index)) {
    int keyType = lua_type(L, -2);

    switch (keyType) {
    case LUA_TSTRING: {
      size_t len;
      const char* data = lua_tolstring(L, -2, &len);
      obj.__isset.stringKeys = true;
      doSerialize(obj.stringKeys[std::string(data, len)], L, -1);
      break;
    }
    case LUA_TBOOLEAN:
      if (lua_toboolean(L, -2)) {
        obj.__isset.trueKey = true;
        doSerialize(obj.trueKey, L, -1);
      } else {
        obj.__isset.falseKey = true;
        doSerialize(obj.falseKey, L, -1);
      }
      break;
    case LUA_TNUMBER: {
      double dval = lua_tonumber(L, -2);
      auto val = int64_t(dval);
      if (double(val) == dval) {
        // Skip over elements we've already seen (list-like)
        if (val < 1 || val > listSize) {
          obj.__isset.intKeys = true;
          doSerialize(obj.intKeys[val], L, -1);
        }
        break;
      }  // else fall through to default (otherKeys)
    }
    default:
      obj.__isset.otherKeys = true;
      obj.otherKeys.emplace_back();
      doSerialize(obj.otherKeys.back().key, L, -2);
      doSerialize(obj.otherKeys.back().value, L, -1);
    }
    lua_pop(L, 1);  // pop value
  }

  lua_settop(L, top);
}

namespace {
int luaWriterToIOBuf(lua_State* L, const void* p, size_t sz, void* ud) {
  auto queue = static_cast<folly::IOBufQueue*>(ud);
  queue->append(folly::IOBuf::copyBuffer(p, sz), true);
  return 0;
}
}  // namespace

void Serializer::doSerializeFunction(LuaFunction& obj,
                                     lua_State* L, int index) {
  lua_pushvalue(L, index);  // function must be at top for lua_dump
  folly::IOBufQueue queue;
  int r = lua_dump(L, luaWriterToIOBuf, &queue);
  if (r != 0) {
    luaL_error(L, "lua_dump error %d", r);
  }
  lua_pop(L, 1);
  obj.bytecode = queue.move();

  for (int i = 1; lua_getupvalue(L, index, i); ++i) {
    obj.upvalues.emplace_back();
    doSerialize(obj.upvalues.back(), L, -1);
    lua_pop(L, 1);
  }
}

int Deserializer::fromThrift(lua_State* L, LuaObject& obj) {
  in_ = std::move(obj);
  doDeserializeRefs(L);
  return doDeserialize(L, in_.value);
}

void Deserializer::doDeserializeRefs(lua_State* L) {
  lua_newtable(L);
  convertedIdx_ = lua_gettop(L);
  for (int i = 0; i < in_.refs.size(); ++i) {
    auto& ref = in_.refs[i];

    auto record = [&] {
      lua_pushvalue(L, -1);
      lua_rawseti(L, convertedIdx_, i + 1);  // 1-based
    };

    if (ref.__isset.stringVal) {
      lua_pushlstring(L, ref.stringVal.data(), ref.stringVal.size());
      record();
    } else if (ref.__isset.tableVal) {
      lua_newtable(L);
      record();
    } else if (ref.__isset.functionVal) {
      if (options_ & NO_BYTECODE) {
        luaL_error(L, "Bytecode deserialization disabled");
      }
      doDeserializeFunction(L, ref.functionVal);
      record();
    } else if (ref.__isset.tensorVal) {
      switch (ref.tensorVal.dataType) {
#define DESERIALIZE_TENSOR(TYPE, VALUE) \
      case thpp::ThriftTensorDataType::VALUE: \
        luaPushTensor(L, thpp::Tensor<TYPE>(ref.tensorVal)); \
        break;
      DESERIALIZE_TENSOR(unsigned char, BYTE)
      DESERIALIZE_TENSOR(int32_t, INT32)
      DESERIALIZE_TENSOR(int64_t, INT64)
      DESERIALIZE_TENSOR(float, FLOAT)
      DESERIALIZE_TENSOR(double, DOUBLE)
#undef DESERIALIZE_TENSOR
      default:
        luaL_error(L, "invalid tensor type");
      }
      record();
    } else if (ref.__isset.storageVal) {
      switch (ref.storageVal.dataType) {
#define DESERIALIZE_STORAGE(TYPE, VALUE) \
      case thpp::ThriftTensorDataType::VALUE: \
        luaPushStorage(L, thpp::Storage<TYPE>(ref.storageVal)); \
        break;
      DESERIALIZE_STORAGE(unsigned char, BYTE)
      DESERIALIZE_STORAGE(int32_t, INT32)
      DESERIALIZE_STORAGE(int64_t, INT64)
      DESERIALIZE_STORAGE(float, FLOAT)
      DESERIALIZE_STORAGE(double, DOUBLE)
#undef DESERIALIZE_STORAGE
      default:
        luaL_error(L, "invalid storage type");
      }
      record();
    } else {
      luaL_error(L, "Invalid reference");
    }
  }

  for (int i = 0; i < in_.refs.size(); ++i) {
    auto& ref = in_.refs[i];

    if (!ref.__isset.tableVal && !ref.__isset.functionVal) {
      continue;
    }

    lua_rawgeti(L, convertedIdx_, i + 1);
    if (ref.__isset.tableVal) {
      doSetTable(L, lua_gettop(L), ref.tableVal);
    } else if (ref.__isset.functionVal) {
      doSetUpvalues(L, lua_gettop(L), ref.functionVal);
    }
    lua_pop(L, 1);
  }
}

int Deserializer::doDeserialize(lua_State* L, LuaPrimitiveObject& obj) {
  if (obj.__isset.refVal) {
    if (obj.refVal < 0 || obj.refVal >= in_.refs.size()) {
      luaL_error(L, "Invalid referernce id %d", int(obj.refVal));
    }
    lua_rawgeti(L, convertedIdx_, obj.refVal + 1);  // 1-based
    DCHECK_NE(lua_type(L, -1), LUA_TNIL);
    return 1;
  }

  if (obj.isNil) {
    lua_pushnil(L);
  } else if (obj.__isset.doubleVal) {
    lua_pushnumber(L, obj.doubleVal);
  } else if (obj.__isset.boolVal) {
    lua_pushboolean(L, obj.boolVal);
  } else if (obj.__isset.stringVal) {
    lua_pushlstring(L, obj.stringVal.data(), obj.stringVal.size());
  } else {
    luaL_error(L, "Invalid primitive");
  }

  return 1;
}

void Deserializer::doSetTable(lua_State* L, int index, LuaTable& obj) {
  if (obj.__isset.listKeys) {
    for (int i = 0; i < obj.listKeys.size(); ++i) {
      doDeserialize(L, obj.listKeys[i]);
      lua_rawseti(L, index, i + 1);
    }
  }
  if (obj.__isset.intKeys) {
    for (auto& p : obj.intKeys) {
      lua_pushinteger(L, p.first);
      doDeserialize(L, p.second);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.stringKeys) {
    for (auto& p : obj.stringKeys) {
      lua_pushlstring(L, p.first.data(), p.first.size());
      doDeserialize(L, p.second);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.trueKey) {
    lua_pushboolean(L, true);
    doDeserialize(L, obj.trueKey);
    lua_rawset(L, index);
  }
  if (obj.__isset.falseKey) {
    lua_pushboolean(L, false);
    doDeserialize(L, obj.falseKey);
    lua_rawset(L, index);
  }
  if (obj.__isset.otherKeys) {
    for (auto& kv : obj.otherKeys) {
      doDeserialize(L, kv.key);
      doDeserialize(L, kv.value);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.metatable) {
    doDeserialize(L, obj.metatable);
    lua_setmetatable(L, index);
  }
  if (obj.__isset.specialKey) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSpecialDeserializationCallbackKey);
    if (lua_isnil(L, -1)) {
      luaL_error(L, "Cannot decode special table, no deserialization callback");
    }
    doDeserialize(L, obj.specialKey);
    if (obj.__isset.specialValue) {
      doDeserialize(L, obj.specialValue);
    } else {
      lua_pushnil(L);
    }
    lua_pushvalue(L, index);
    lua_call(L, 3, 0);
  }
}

namespace {
const char* luaReaderFromIOBuf(lua_State* L, void* ud, size_t* sz) {
  auto cursor = static_cast<folly::io::Cursor*>(ud);
  auto p = cursor->peek();
  if (p.second == 0) {
    *sz = 0;
    return nullptr;
  } else {
    *sz = p.second;
    cursor->skip(p.second);
    return reinterpret_cast<const char*>(p.first);
  }
}
}  // namespace

void Deserializer::doDeserializeFunction(lua_State* L, LuaFunction& obj) {
  folly::io::Cursor cursor(obj.bytecode.get());
  int r = lua_load(L, luaReaderFromIOBuf, &cursor, "<thrift>");
  if (r != 0) {
    luaL_error(L, "lua_load error %d", r);
  }
}

void Deserializer::doSetUpvalues(lua_State* L, int idx, LuaFunction& obj) {
  for (int i = 0; i < obj.upvalues.size(); ++i) {
    doDeserialize(L, obj.upvalues[i]);
    auto r = lua_setupvalue(L, idx, i + 1);
    if (!r) {
      luaL_error(L, "too many upvalues");
    }
  }
}

}}  // namespaces
