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

#define XLOG_LEVEL 4

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
  doSerialize(out_.value, L, index, 0);
  converted_.clear();
  return std::move(out_);
}

namespace {

std::string indent(int level) {
  return std::string(level * 2, ' ');
}

}  // namespace

#define XLOG DVLOG(XLOG_LEVEL) << "S: " << indent(level)

void Serializer::doSerialize(LuaPrimitiveObject& obj, lua_State* L, int index,
                             int level) {
  if (index < 0) {
    index = lua_gettop(L) + index + 1;
  }

  LuaRefObject ref;
  int64_t refIdx = -1;
  // Check if we've encountered it before, record if not
  if (auto luaPtr = lua_topointer(L, index)) {
    auto pos = converted_.find(luaPtr);
    if (pos != converted_.end()) {
      XLOG << "existing reference " << pos->second;
      obj.refVal = pos->second;
      obj.__isset.refVal = true;
      return;
    }
    refIdx = out_.refs.size();
    converted_[luaPtr] = refIdx;
    obj.__isset.refVal = true;
    obj.refVal = refIdx;
    out_.refs.emplace_back();
    XLOG << "new reference " << refIdx;
  }

  int type = lua_type(L, index);
  switch (type) {
  case LUA_TNIL:
    DCHECK_EQ(refIdx, -1);
    XLOG << "nil";
    obj.isNil = true;
    break;  // we're done!
  case LUA_TNUMBER:
    DCHECK_EQ(refIdx, -1);
    obj.doubleVal = lua_tonumber(L, index);
    XLOG << "number " << obj.doubleVal;
    obj.__isset.doubleVal = true;
    break;
  case LUA_TBOOLEAN:
    DCHECK_EQ(refIdx, -1);
    obj.boolVal = lua_toboolean(L, index);
    XLOG << "boolean " << obj.boolVal;
    obj.__isset.boolVal = true;
    break;
  case LUA_TSTRING: {
    size_t len;
    const char* data = lua_tolstring(L, index, &len);
    XLOG << "string [" << folly::StringPiece(data, len) << "]";
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
    XLOG << "table";
    doSerializeTable(ref.tableVal, L, index, level);
    break;
  case LUA_TUSERDATA:
    DCHECK_GE(refIdx, 0);
#define SERIALIZE_TENSOR(TYPE) \
    { \
      auto tensor = luaGetTensor<TYPE>(L, index); \
      if (tensor) { \
        XLOG << "Tensor<" #TYPE ">"; \
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
        XLOG << "Storage<" #TYPE ">"; \
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
    XLOG << "function";
    doSerializeFunction(ref.functionVal, L, index, level);
    break;
  default:
    luaL_error(L, "invalid type %d", type);
  }

  if (refIdx >= 0) {
    out_.refs[refIdx] = std::move(ref);
  }
}

void Serializer::doSerializeTable(LuaTable& obj,
                                  lua_State* L, int index, int level) {
  int top = lua_gettop(L);

  int n = lua_getmetatable(L, index);
  if (n) {
    // This table has a metatable. Check the special callback first, to
    // see if we need to do anything interesting with it.
    int metatableIdx = lua_gettop(L);

    lua_getfield(L, LUA_REGISTRYINDEX, kSpecialSerializationCallbackKey);
    if (!lua_isnil(L, -1)) {
      XLOG << "has metatable";
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
        XLOG << "special key";
        doSerialize(obj.specialKey, L, retKeyIdx, level + 1);
      }

      if (!lua_isnil(L, retValIdx)) {
        obj.__isset.specialValue = true;
        XLOG << "special value";
        doSerialize(obj.specialValue, L, retValIdx, level + 1);
      }

      // nil = serialize current metatable
      // false = serialize no metatable
      if (!lua_isnil(L, retMetatableIdx)) {
        if (!lua_toboolean(L, retMetatableIdx)) {  // check if false
          XLOG << "special: no metatable";
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
      XLOG << "metatable";
      doSerialize(obj.metatable, L, metatableIdx, level + 1);
    }
  }

  // Get list-like elements (consecutive integers, starting at 1)
  auto listSize = lua_objlen(L, index);
  auto lastDenseIndex = 0;
  XLOG << "listSize = " << listSize;
  if (listSize > 0) {
    obj.__isset.listKeys = true;
    obj.listKeys.reserve(listSize);
    for (int i = 1; i <= listSize; ++i) {
      lua_rawgeti(L, index, i);
      if (lua_isnil(L, -1)) {
        // lua_objlen is undefined for any kind of sparse indices, so remember
        // how far we got.
        lastDenseIndex = i - 1;
        lua_pop(L, 1);
        break;
      }
      XLOG << "(list) [" << i << "]";
      obj.listKeys.emplace_back();
      doSerialize(obj.listKeys.back(), L, -1, level + 1);
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
      XLOG << "(string) [" << folly::StringPiece(data, len) << "]";
      doSerialize(obj.stringKeys[std::string(data, len)], L, -1, level + 1);
      break;
    }
    case LUA_TBOOLEAN:
      if (lua_toboolean(L, -2)) {
        obj.__isset.trueKey = true;
        XLOG << "(boolean) [true]";
        doSerialize(obj.trueKey, L, -1, level + 1);
      } else {
        obj.__isset.falseKey = true;
        XLOG << "(boolean) [false]";
        doSerialize(obj.falseKey, L, -1, level + 1);
      }
      break;
    case LUA_TNUMBER: {
      double dval = lua_tonumber(L, -2);
      auto val = int64_t(dval);
      if (double(val) == dval) {
        // Skip over elements we've already seen (list-like)
        if (val < 1 || val > lastDenseIndex) {
          obj.__isset.intKeys = true;
          XLOG << "(int) [" << val << "]";
          doSerialize(obj.intKeys[val], L, -1, level + 1);
        }
        break;
      }  // else fall through to default (otherKeys)
    }
    default:
      obj.__isset.otherKeys = true;
      obj.otherKeys.emplace_back();
      XLOG << "(other) key";
      doSerialize(obj.otherKeys.back().key, L, -2, level + 1);
      XLOG << "(other) value";
      doSerialize(obj.otherKeys.back().value, L, -1, level + 1);
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
                                     lua_State* L, int index, int level) {
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
    XLOG << "upvalue " << i;
    doSerialize(obj.upvalues.back(), L, -1, level + 1);
    lua_pop(L, 1);
  }
}

#undef XLOG

int Deserializer::fromThrift(lua_State* L, LuaObject&& obj) {
  in_ = std::move(obj);
  doDeserializeRefs(L);

  auto retval = doDeserialize(L, in_.value, 0);
  lua_remove(L, -2);
  lua_remove(L, -2);
  return retval;
}

void Deserializer::doDeserializeRefs(lua_State* L) {
  lua_newtable(L);
  convertedIdx_ = lua_gettop(L);
  for (int i = 0; i < in_.refs.size(); ++i) {
#define XLOG DVLOG(XLOG_LEVEL) << "D: reference " << i << ": "
    auto& ref = in_.refs[i];

    auto record = [&] {
      lua_pushvalue(L, -1);
      lua_rawseti(L, convertedIdx_, i + 1);  // 1-based
    };

    if (ref.__isset.stringVal) {
      XLOG << "string [" << ref.stringVal << "]";
      lua_pushlstring(L, ref.stringVal.data(), ref.stringVal.size());
      record();
    } else if (ref.__isset.tableVal) {
      XLOG << "table";
      lua_newtable(L);
      record();
    } else if (ref.__isset.functionVal) {
      if (options_ & NO_BYTECODE) {
        luaL_error(L, "Bytecode deserialization disabled");
      }
      XLOG << "function";
      doDeserializeFunction(L, ref.functionVal);
      record();
    } else if (ref.__isset.tensorVal) {
      switch (ref.tensorVal.dataType) {
#define DESERIALIZE_TENSOR(TYPE, VALUE) \
      case thpp::ThriftTensorDataType::VALUE: \
        XLOG << "Tensor<" #TYPE ">"; \
        luaPushTensor(L, thpp::Tensor<TYPE>(std::move(ref.tensorVal))); \
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
        XLOG << "Storage<" #TYPE ">"; \
        luaPushStorage(L, thpp::Storage<TYPE>(std::move(ref.storageVal))); \
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
      XLOG << "table data";
      doSetTable(L, lua_gettop(L), ref.tableVal);
    } else if (ref.__isset.functionVal) {
      XLOG << "function upvalues";
      doSetUpvalues(L, lua_gettop(L), ref.functionVal);
    }
    lua_pop(L, 1);
  }
}

#undef XLOG
#define XLOG DVLOG(XLOG_LEVEL) << "D: " << indent(level)
int Deserializer::doDeserialize(lua_State* L, LuaPrimitiveObject& obj,
                                int level) {
  if (obj.__isset.refVal) {
    if (obj.refVal < 0 || obj.refVal >= in_.refs.size()) {
      luaL_error(L, "Invalid referernce id %d", int(obj.refVal));
    }
    XLOG << "reference " << obj.refVal;
    lua_rawgeti(L, convertedIdx_, obj.refVal + 1);  // 1-based
    DCHECK_NE(lua_type(L, -1), LUA_TNIL);
    return 1;
  }

  if (obj.isNil) {
    XLOG << "nil";
    lua_pushnil(L);
  } else if (obj.__isset.doubleVal) {
    XLOG << "number " << obj.doubleVal;
    lua_pushnumber(L, obj.doubleVal);
  } else if (obj.__isset.boolVal) {
    XLOG << "boolean " << obj.boolVal;
    lua_pushboolean(L, obj.boolVal);
  } else if (obj.__isset.stringVal) {
    XLOG << "string [" << obj.stringVal << "]";
    lua_pushlstring(L, obj.stringVal.data(), obj.stringVal.size());
  } else {
    luaL_error(L, "Invalid primitive");
  }

  return 1;
}

#undef XLOG
#define XLOG DVLOG(XLOG_LEVEL) << "D:   "

void Deserializer::doSetTable(lua_State* L, int index, LuaTable& obj) {
  if (obj.__isset.listKeys) {
    for (int i = 0; i < obj.listKeys.size(); ++i) {
      XLOG << "(list) [" << i + 1 << "]";
      doDeserialize(L, obj.listKeys[i], 2);
      lua_rawseti(L, index, i + 1);
    }
  }
  if (obj.__isset.intKeys) {
    for (auto& p : obj.intKeys) {
      XLOG << "(int) [" << p.first << "]";
      lua_pushinteger(L, p.first);
      doDeserialize(L, p.second, 2);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.stringKeys) {
    for (auto& p : obj.stringKeys) {
      XLOG << "(string) [" << p.first << "]";
      lua_pushlstring(L, p.first.data(), p.first.size());
      doDeserialize(L, p.second, 2);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.trueKey) {
    XLOG << "(boolean) [true]";
    lua_pushboolean(L, true);
    doDeserialize(L, obj.trueKey, 2);
    lua_rawset(L, index);
  }
  if (obj.__isset.falseKey) {
    XLOG << "(boolean) [false]";
    lua_pushboolean(L, false);
    doDeserialize(L, obj.falseKey, 2);
    lua_rawset(L, index);
  }
  if (obj.__isset.otherKeys) {
    for (auto& kv : obj.otherKeys) {
      XLOG << "(other) key";
      doDeserialize(L, kv.key, 2);
      XLOG << "(other) value";
      doDeserialize(L, kv.value, 2);
      lua_rawset(L, index);
    }
  }
  if (obj.__isset.metatable) {
    XLOG << "metatable";
    doDeserialize(L, obj.metatable, 2);
    lua_setmetatable(L, index);
  }
  if (obj.__isset.specialKey) {
    XLOG << "special key";
    lua_getfield(L, LUA_REGISTRYINDEX, kSpecialDeserializationCallbackKey);
    if (lua_isnil(L, -1)) {
      luaL_error(L, "Cannot decode special table, no deserialization callback");
    }
    doDeserialize(L, obj.specialKey, 2);
    if (obj.__isset.specialValue) {
      XLOG << "special value";
      doDeserialize(L, obj.specialValue, 2);
    } else {
      lua_pushnil(L);
    }
    lua_pushvalue(L, index);
    lua_call(L, 3, 0);
  }
}

#undef XLOG

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
    doDeserialize(L, obj.upvalues[i], 2);
    auto r = lua_setupvalue(L, idx, i + 1);
    if (!r) {
      luaL_error(L, "too many upvalues");
    }
  }
}

}}  // namespaces
