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

// We use the (unique in this address space) addresses of these
// variables as keys in the registry, to guarantee no conflicts.
char kSpecialSerializationCallbackKey;
char kSpecialDeserializationCallbackKey;
char kUserDataCallbackKey;

}  // namespace

void setSpecialSerializationCallback(lua_State* L, int index) {
  index = luaRealIndex(L, index);
  lua_pushlightuserdata(L, &kSpecialSerializationCallbackKey);
  lua_pushvalue(L, index);
  lua_settable(L, LUA_REGISTRYINDEX);
}

void setSpecialDeserializationCallback(lua_State* L, int index) {
  index = luaRealIndex(L, index);
  lua_pushlightuserdata(L, &kSpecialDeserializationCallbackKey);
  lua_pushvalue(L, index);
  lua_settable(L, LUA_REGISTRYINDEX);
}

namespace {

int constructUserDataCallbackTable(lua_State* L) {
  lua_createtable(L, 2, 0);  // tab

  // Create keytab and set it as tab[1]
  lua_newtable(L);  // tab keytab
  lua_rawseti(L, -2, 1);  // tab

  // Create mttab
  lua_newtable(L);  // tab mttab

  // Set up mttab to use weak keys, by setting the mettable to {__mode = 'k'}
  lua_newtable(L);  // tab mttab mttab-mt
  lua_pushstring(L, "k");  // tab mttab mttab-mt "k"
  lua_setfield(L, -2, "__mode");  // tab mttab mttab-mt
  lua_setmetatable(L, -2);  // tab mttab

  // Set mttab as tab[2]
  lua_rawseti(L, -2, 2);  // tab
  return 1;
}

// Look up key at index keyIndex in table at index tableIndex and
// push the value on the stack. Also return true iff the value is
// non-nil. Does not obey metatable.
bool getTable(lua_State* L, int tableIndex, int keyIndex) {
  tableIndex = luaRealIndex(L, tableIndex);
  keyIndex = luaRealIndex(L, keyIndex);
  lua_pushvalue(L, keyIndex);
  lua_rawget(L, tableIndex);
  return !lua_isnil(L, -1);
}

// Similar to getTable, but only push the value onto the stack if it exists
// (non-nil)
bool maybeGetTable(lua_State* L, int tableIndex, int keyIndex) {
  bool exists = getTable(L, tableIndex, keyIndex);
  if (!exists) {
    lua_pop(L, 1);
  }
  return exists;
}

// Check if key at index keyIndex exists in table at index tableIndex.
// Does not modify the stack. Does not obey metatables.
bool keyExists(lua_State* L, int tableIndex, int keyIndex) {
  bool exists = getTable(L, tableIndex, keyIndex);
  lua_pop(L, 1);
  return exists;
}

}  // namespace

void registerUserDataCallbacks(
    lua_State* L,
    folly::StringPiece key,
    int mtIndex,
    UserDataSerializer serializer,
    UserDataDeserializer deserializer) {
  mtIndex = luaRealIndex(L, mtIndex);
  // We store at kUserDataCallbackKey in the registry a table:
  // {
  //   { key -> { metatable, serializer, deserializer } },  -- "keytab"
  //   { metatable -> key },  -- "mttab", weak keys
  // }
  lua_pushlightuserdata(L, &kUserDataCallbackKey);
  lua_gettable(L, LUA_REGISTRYINDEX);  // prev_tab
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    constructUserDataCallbackTable(L);
    // Save tab in registry
    lua_pushlightuserdata(L, &kUserDataCallbackKey);
    lua_pushvalue(L, -2);  // tab k tab
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  // tab

  // Check if metatable is already registered
  lua_rawgeti(L, -1, 2);  // tab mttab
  if (keyExists(L, -1, mtIndex)) {
    luaL_error(L, "Custom user data metatable already registered");
  }

  // Add key -> {mt, serializer, deserializer} mapping in keytab
  lua_rawgeti(L, -2, 1);  // tab mttab keytab
  lua_pushlstring(L, key.data(), key.size());  // tab mttab keytab key

  // Check if key is already registered
  if (keyExists(L, -2, -1)) {
    luaL_error(L, "Custom user data key already registered");
  }

  // Create {mt, serializer, deserializer} table
  lua_createtable(L, 3, 0);  // tab mttab keytab key new_value

  lua_pushvalue(L, mtIndex);  // tab mttab keytab key new_value mt
  lua_rawseti(L, -2, 1);  // new_value[1] = mt
  // tab mttab keytab key new_value

  lua_pushlightuserdata(L, reinterpret_cast<void*>(serializer));
  // tab mttab keytab key new_value serializer
  lua_rawseti(L, -2, 2);  // new_value[2] = serializer
  // tab mttab keytab key new_value

  lua_pushlightuserdata(L, reinterpret_cast<void*>(deserializer));
  // tab mttab keytab key new_value deserializer
  lua_rawseti(L, -2, 3);  // new_value[3] = deserializer
  // tab mttab keytab key new_value

  // Actually set keytab[key] = {mt, serializer, deserializer}
  lua_rawset(L, -3);  // tab mttab keytab
  lua_pop(L, 1);  // tab mttab

  // Add mt -> key mapping in mttab
  lua_pushvalue(L, mtIndex);  // tab mttab mt
  lua_pushlstring(L, key.data(), key.size());  // tab mttab mt key
  lua_rawset(L, -3);  // tab mttab

  // Clean up
  lua_pop(L, 2);
}

void unregisterUserDataCallbacks(
    lua_State* L,
    folly::StringPiece key) {
  LuaStackGuard guard(L);

  lua_pushlightuserdata(L, &kUserDataCallbackKey);
  lua_gettable(L, LUA_REGISTRYINDEX);  // tab
  if (lua_isnil(L, -1)) {  // nothing registered
    return;
  }

  lua_rawgeti(L, -1, 2);  // tab mttab
  lua_rawgeti(L, -2, 1);  // tab mttab keytab

  // Look up any existing mapping for key
  lua_pushlstring(L, key.data(), key.size());  // tab mttab keytab key
  if (!getTable(L, -2, -1)) {
    return;
  }

  // prev_value is {mt, serializer, deserializer}
  // Retrieve mt
  lua_rawgeti(L, -1, 1);  // tab mttab keytab key prev_value mt

  // Clear mapping for mt
  lua_pushnil(L);  // tab mttab keytab key prev_value mt nil
  lua_rawset(L, -6);  // set mttable[mt] = nil
  // tab mttab keytab key prev_value

  lua_pop(L, 1);  // tab mttab keytab key

  // Clear mapping for key
  lua_pushnil(L);  // tab mttab keytab key nil
  lua_rawset(L, -3);  // set keytab[key] = nil
  // tab mttab keytab
}

void Serializer::setInvertedEnv(int invEnvIdx) {
  bool set = false;

  if (invEnvIdx != 0) {
    invEnvIdx = luaRealIndex(L_, invEnvIdx);
    set = !lua_isnil(L_, invEnvIdx);
  }

  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);

  if (set) {
    lua_pushvalue(L_, invEnvIdx);
  } else {
    lua_pushnil(L_);
  }

  lua_rawseti(L_, -2, 2);
  lua_pop(L_, 1);
}

Serializer::Serializer(lua_State* L, unsigned int options)
  : L_(L),
    options_(options) {
  // Store associated state in registry under key == this:
  // { converted_cache, inverted_env }
  //
  // converted_cache is a map from objects that have already been serialized
  // to the index where they are stored in refs_.
  //
  // inverted_env is a map from objects that should not be serialized
  // to the unique key (tuple of two primitive values) that they should
  // be replaced with.
  lua_pushlightuserdata(L_, this);
  lua_createtable(L_, 2, 0);
  // Create converted cache
  lua_newtable(L_);
  lua_rawseti(L_, -2, 1);
  lua_settable(L_, LUA_REGISTRYINDEX);
}

Serializer::~Serializer() {
  lua_pushlightuserdata(L_, this);
  lua_pushnil(L_);
  lua_settable(L_, LUA_REGISTRYINDEX);
}

LuaObject Serializer::toThrift(lua_State* L, int index, int invEnvIdx,
                               unsigned int options) {
  LuaObject out;

  Serializer serializer(L, options);
  serializer.setInvertedEnv(invEnvIdx);
  out.value = serializer.serialize(index);
  out.refs = serializer.finish();

  return out;
}

LuaPrimitiveObject Serializer::serialize(int index) {
  int top = lua_gettop(L_);
  index = luaRealIndex(L_, index);

  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, 1);  // converted
  lua_rawgeti(L_, -2, 2);  // inverted env

  SerializationContext ctx;
  ctx.convertedIdx = lua_gettop(L_) - 1;
  ctx.invEnvIdx = lua_isnil(L_, -1) ? 0 : ctx.convertedIdx + 1;

  LuaPrimitiveObject out;
  doSerialize(out, index, ctx, 0);

  DCHECK_EQ(lua_gettop(L_), top + 3);
  lua_pop(L_, 3);

  return out;
}

LuaRefList Serializer::finish() {
  // Clear converted cache
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_newtable(L_);
  lua_rawseti(L_, -2, 1);
  lua_pop(L_, 1);
  return std::move(refs_);
}

#define XLOG DVLOG(XLOG_LEVEL) << "S: " << indent(level)

namespace {

std::string indent(int level) {
  return std::string(level * 2, ' ');
}

bool serializeUserData(lua_State* L, int index, int mtIndex,
                       int level, LuaRefObject& ref) {
  LuaStackGuard guard(L);

  lua_pushlightuserdata(L, &kUserDataCallbackKey);
  lua_gettable(L, LUA_REGISTRYINDEX);  // tab
  if (lua_isnil(L, -1)) {
    return false;
  }

  lua_rawgeti(L, -1, 2);  // tab mttab
  lua_rawgeti(L, -2, 1);  // tab mttab keytab

  // Look up key given userdata metatable (as mttab[mt])
  lua_pushvalue(L, mtIndex);  // tab mttab keytab mt
  lua_rawget(L, -3);  // tab mttab keytab key
  if (lua_isnil(L, -1)) {  // not found
    return false;
  }
  DCHECK_EQ(lua_type(L, -1), LUA_TSTRING);

  // Retrieve serde ({mt, serializer, deserializer}) given key
  lua_pushvalue(L, -1);  // tab mttab keytab key key
  lua_rawget(L, -3);  // tab mttab keytab key serde
  DCHECK_EQ(lua_type(L, -1), LUA_TTABLE);

#ifndef NDEBUG
  // In debug mode, verify that the metatable actually matches
  lua_rawgeti(L, -1, 1);
  DCHECK(lua_rawequal(L, -1, mtIndex));
  lua_pop(L, 1);
#endif

  // tab mttab keytab key serde
  // Retrieve serializer (serde[2])
  lua_rawgeti(L, -1, 2);  // tab mttab keytab key serde serializer
  auto serializer = reinterpret_cast<UserDataSerializer>(
      lua_topointer(L, -1));
  lua_pop(L, 2);  // tab mttab keytab key

  // Call serializer
  int prevTop = lua_gettop(L);
  auto buf = serializer(L, index);
  if (lua_gettop(L) != prevTop) {
    luaL_error(L, "serializer did not leave stack unchanged");
  }

  ref.__isset.customUserDataVal = true;
  auto& cud = ref.customUserDataVal;

  // Key is still at the top of the stack
  size_t len;
  auto str = lua_tolstring(L, -1, &len);
  cud.key.assign(str, len);
  XLOG << "custom userdata [" << cud.key << "]";
  cud.value = std::move(buf);

  return true;
}

}  // namespace

void Serializer::doSerialize(LuaPrimitiveObject& obj, int index,
                             const SerializationContext& ctx,
                             int level, bool allowRefs) {
  index = luaRealIndex(L_, index);

  LuaRefObject ref;
  int64_t refIdx = -1;

  // Check if we've encountered it before, record if not
  const void* luaPtr = nullptr;
  if (allowRefs && (luaPtr = lua_topointer(L_, index)) != nullptr) {
    if (maybeGetTable(L_, ctx.convertedIdx, index)) {
      DCHECK_EQ(lua_type(L_, -1), LUA_TNUMBER);
      auto r = lua_tointeger(L_, -1);
      lua_pop(L_, 1);
      XLOG << "existing reference " << r;
      obj.refVal = r;
      obj.__isset.refVal = true;
      return;
    }

    refIdx = refs_.size();

    lua_pushvalue(L_, index);
    lua_pushinteger(L_, refIdx);
    lua_rawset(L_, ctx.convertedIdx);

    obj.__isset.refVal = true;
    obj.refVal = refIdx;

    // Reserve spot at refIdx, we'll fill after children are serialized
    // (postorder)
    refs_.emplace_back();
    XLOG << "new reference " << refIdx;

    bool found = false;

    if (ctx.invEnvIdx != 0) {
      LuaStackGuard guard(L_);

      if (maybeGetTable(L_, ctx.invEnvIdx, index)) {
        found = true;
        XLOG << "external env value";
        ref.__isset.envLocation = true;
        int keysIdx = lua_gettop(L_);
        DCHECK_EQ(lua_type(L_, -1), LUA_TTABLE);
        DCHECK_EQ(lua_objlen(L_, keysIdx), 2);
        lua_rawgeti(L_, keysIdx, 1);
        doSerialize(ref.envLocation.env, -1, ctx, level + 1, false);
        lua_rawgeti(L_, keysIdx, 2);
        doSerialize(ref.envLocation.key, -1, ctx, level + 1, false);
      }
    }

    if (found) {
      refs_[refIdx] = std::move(ref);
      return;
    }
  }

  int type = lua_type(L_, index);
  switch (type) {
  case LUA_TNIL:
    DCHECK_EQ(refIdx, -1);
    XLOG << "nil";
    obj.isNil = true;
    break;  // we're done!
  case LUA_TNUMBER:
    DCHECK_EQ(refIdx, -1);
    obj.doubleVal = lua_tonumber(L_, index);
    XLOG << "number " << obj.doubleVal;
    obj.__isset.doubleVal = true;
    break;
  case LUA_TBOOLEAN:
    DCHECK_EQ(refIdx, -1);
    obj.boolVal = lua_toboolean(L_, index);
    XLOG << "boolean " << obj.boolVal;
    obj.__isset.boolVal = true;
    break;
  case LUA_TSTRING: {
    size_t len;
    const char* data = lua_tolstring(L_, index, &len);
    XLOG << "string [" << folly::StringPiece(data, len) << "]";
    // Strings may be references or not, depending on whether they're interned
    if (refIdx == -1) {
      obj.stringVal.assign(data, len);
      obj.__isset.stringVal = true;
    } else {
      DCHECK(allowRefs);
      ref.stringVal.assign(data, len);
      ref.__isset.stringVal = true;
    }
    break;
  }
  case LUA_TTABLE:
    if (!allowRefs) {
      luaL_error(L_, "references not allowed (table)");
    }
    DCHECK_GE(refIdx, 0);
    ref.__isset.tableVal = true;
    XLOG << "table";
    doSerializeTable(ref.tableVal, index, ctx, level);
    break;
  case LUA_TUSERDATA:
    if (!allowRefs) {
      luaL_error(L_, "references not allowed (userdata)");
    }
    DCHECK_GE(refIdx, 0);
#define SERIALIZE_TENSOR(TYPE) \
    { \
      auto tensor = luaGetTensor<TYPE>(L_, index); \
      if (tensor) { \
        XLOG << "Tensor<" #TYPE ">"; \
        ref.__isset.tensorVal = true; \
        tensor->serialize(ref.tensorVal, thpp::ThriftTensorEndianness::NATIVE, \
                          mayShare()); \
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
      auto storage = luaGetStorage<TYPE>(L_, index); \
      if (storage) { \
        XLOG << "Storage<" #TYPE ">"; \
        ref.__isset.storageVal = true; \
        storage->serialize(ref.storageVal, \
                           thpp::ThriftTensorEndianness::NATIVE, mayShare()); \
        break; \
      } \
    }
    SERIALIZE_STORAGE(unsigned char)
    SERIALIZE_STORAGE(int32_t)
    SERIALIZE_STORAGE(int64_t)
    SERIALIZE_STORAGE(float)
    SERIALIZE_STORAGE(double)
#undef SERIALIZE_STORAGE

    if (lua_getmetatable(L_, index)) {
      // Try custom userdata serializer for this metatable
      bool done = serializeUserData(L_, index, lua_gettop(L_), level, ref);
      lua_pop(L_, 1);  // pop metatable
      if (done) {
        break;
      }
    }
    luaL_error(L_, "invalid userdata");
    break;
  case LUA_TFUNCTION:
    if (!allowRefs) {
      luaL_error(L_, "references not allowed (function)");
    }
    DCHECK_GE(refIdx, 0);
    ref.__isset.functionVal = true;
    XLOG << "function";
    doSerializeFunction(ref.functionVal, index, ctx, level);
    break;
  default:
    luaL_error(L_, "invalid type %d", type);
  }

  if (refIdx >= 0) {
    refs_[refIdx] = std::move(ref);
  }
}

void Serializer::doSerializeTable(LuaTable& obj,
                                  int index,
                                  const SerializationContext& ctx,
                                  int level) {
  int top = lua_gettop(L_);

  int n = lua_getmetatable(L_, index);
  if (n) {
    // This table has a metatable. Check the special callback first, to
    // see if we need to do anything interesting with it.
    int metatableIdx = lua_gettop(L_);

    lua_pushlightuserdata(L_, &kSpecialSerializationCallbackKey);
    lua_gettable(L_, LUA_REGISTRYINDEX);
    if (!lua_isnil(L_, -1)) {
      XLOG << "has metatable";
      // We have a special callback. Call it:
      //
      // key, value, table, metatable = callback(table)
      lua_pushvalue(L_, index);  // table
      lua_call(L_, 1, 4);
      int retMetatableIdx = lua_gettop(L_);
      int retTableIdx = retMetatableIdx - 1;
      int retValIdx = retMetatableIdx - 2;
      int retKeyIdx = retMetatableIdx - 3;

      if (!lua_isnil(L_, retKeyIdx)) {
        obj.__isset.specialKey = true;
        XLOG << "special key";
        doSerialize(obj.specialKey, retKeyIdx, ctx, level + 1);
      }

      if (!lua_isnil(L_, retValIdx)) {
        obj.__isset.specialValue = true;
        XLOG << "special value";
        doSerialize(obj.specialValue, retValIdx, ctx, level + 1);
      }

      // nil = serialize current metatable
      // false = serialize no metatable
      if (!lua_isnil(L_, retMetatableIdx)) {
        if (!lua_toboolean(L_, retMetatableIdx)) {  // check if false
          XLOG << "special: no metatable";
          metatableIdx = 0;
        } else {
          metatableIdx = retMetatableIdx;
        }
      }
      if (!lua_isnil(L_, retTableIdx)) {
        index = retTableIdx;
      }
    }
    if (metatableIdx) {
      obj.__isset.metatable = true;
      XLOG << "metatable";
      doSerialize(obj.metatable, metatableIdx, ctx, level + 1);
    }
  }

  // Get list-like elements (consecutive integers, starting at 1)
  auto listSize = lua_objlen(L_, index);
  auto lastDenseIndex = listSize;
  XLOG << "listSize = " << listSize;
  if (listSize > 0) {
    obj.listKeys.reserve(listSize);
    for (int i = 1; i <= listSize; ++i) {
      lua_rawgeti(L_, index, i);
      if (lua_isnil(L_, -1)) {
        // lua_objlen is undefined for any kind of sparse indices, so remember
        // how far we got.
        lastDenseIndex = i - 1;
        lua_pop(L_, 1);
        break;
      }
      XLOG << "(list) [" << i << "]";
      obj.__isset.listKeys = true;
      obj.listKeys.emplace_back();
      doSerialize(obj.listKeys.back(), -1, ctx, level + 1);
      lua_pop(L_, 1);
    }
  }

  // Iterate through other elements in the table
  lua_pushnil(L_);
  while (lua_next(L_, index)) {
    int keyType = lua_type(L_, -2);

    switch (keyType) {
    case LUA_TSTRING: {
      size_t len;
      const char* data = lua_tolstring(L_, -2, &len);
      obj.__isset.stringKeys = true;
      XLOG << "(string) [" << folly::StringPiece(data, len) << "]";
      doSerialize(obj.stringKeys[std::string(data, len)], -1, ctx,
                  level + 1);
      break;
    }
    case LUA_TBOOLEAN:
      if (lua_toboolean(L_, -2)) {
        obj.__isset.trueKey = true;
        XLOG << "(boolean) [true]";
        doSerialize(obj.trueKey, -1, ctx, level + 1);
      } else {
        obj.__isset.falseKey = true;
        XLOG << "(boolean) [false]";
        doSerialize(obj.falseKey, -1, ctx, level + 1);
      }
      break;
    case LUA_TNUMBER: {
      double dval = lua_tonumber(L_, -2);
      auto val = int64_t(dval);
      if (double(val) == dval) {
        // Skip over elements we've already seen (list-like)
        if (val < 1 || val > lastDenseIndex) {
          obj.__isset.intKeys = true;
          XLOG << "(int) [" << val << "]";
          doSerialize(obj.intKeys[val], -1, ctx, level + 1);
        }
        break;
      }  // else (not integer) fall through to default (otherKeys)
    }
    default:
      obj.__isset.otherKeys = true;
      obj.otherKeys.emplace_back();
      XLOG << "(other) key";
      doSerialize(obj.otherKeys.back().key, -2, ctx, level + 1);
      XLOG << "(other) value";
      doSerialize(obj.otherKeys.back().value, -1, ctx, level + 1);
    }
    lua_pop(L_, 1);  // pop value
  }

  lua_settop(L_, top);
}

namespace {
int luaWriterToIOBuf(lua_State* L, const void* p, size_t sz, void* ud) {
  auto queue = static_cast<folly::IOBufQueue*>(ud);
  queue->append(folly::IOBuf::copyBuffer(p, sz), true);
  return 0;
}
}  // namespace

void Serializer::doSerializeFunction(LuaFunction& obj,
                                     int index,
                                     const SerializationContext& ctx,
                                     int level) {
  lua_pushvalue(L_, index);  // function must be at top for lua_dump
  folly::IOBufQueue queue;
  int r = lua_dump(L_, luaWriterToIOBuf, &queue);
  if (r != 0) {
    luaL_error(L_, "lua_dump error %d", r);
  }
  lua_pop(L_, 1);
  obj.bytecode = std::move(*queue.move());

  for (int i = 1; lua_getupvalue(L_, index, i); ++i) {
    obj.upvalues.emplace_back();
    XLOG << "upvalue " << i;
    doSerialize(obj.upvalues.back(), -1, ctx, level + 1);
    lua_pop(L_, 1);
  }
}

#undef XLOG

Deserializer::Deserializer(lua_State* L, unsigned int options)
  : L_(L),
    refs_(nullptr),
    options_(options) {
  // Store in the registry a 2-element table: converted cache and
  // env.
  lua_pushlightuserdata(L_, this);  // this
  lua_createtable(L_, 2, 0);   // this tab
  lua_newtable(L_);  // this tab converted
  lua_rawseti(L_, -2, 1);  // this tab
  lua_settable(L_, LUA_REGISTRYINDEX);
}

void Deserializer::start(const LuaRefList* refs) {
  DCHECK(!refs_);
  DCHECK(refs);
  refs_ = refs;

  doDeserializeRefs();
}

Deserializer::~Deserializer() {
  lua_pushlightuserdata(L_, this);
  lua_pushnil(L_);
  lua_settable(L_, LUA_REGISTRYINDEX);
}

void Deserializer::setEnv(int envIdx) {
  DCHECK(!refs_);
  bool set = false;

  if (envIdx != 0) {
    envIdx = luaRealIndex(L_, envIdx);
    set = !lua_isnil(L_, envIdx);
  }

  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);

  if (set) {
    lua_pushvalue(L_, envIdx);
  } else {
    lua_pushnil(L_);
  }

  lua_rawseti(L_, -2, 2);
  lua_pop(L_, 1);
}

void Deserializer::finish() {
  DCHECK(refs_);
  // Clear converted cache
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_newtable(L_);
  lua_rawseti(L_, -2, 1);
  lua_pop(L_, 1);
  refs_ = nullptr;
}

int Deserializer::fromThrift(lua_State* L, const LuaObject& obj,
                             int envIdx, unsigned int options) {
  Deserializer deserializer(L, options);
  deserializer.setEnv(envIdx);
  deserializer.start(&obj.refs);
  return deserializer.deserialize(obj.value);
}

int Deserializer::deserialize(const LuaPrimitiveObject& obj) {
  int top = lua_gettop(L_);
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, 1);
  lua_remove(L_, -2);
  int convertedIdx = lua_gettop(L_);
  auto retval = doDeserialize(obj, convertedIdx, 0);
  lua_remove(L_, convertedIdx);
  DCHECK_EQ(lua_gettop(L_), top + retval);  // hygienic
  return retval;
}

namespace {

bool deserializeUserData(lua_State* L, const LuaUserData& cud) {
  lua_pushlightuserdata(L, &kUserDataCallbackKey);
  lua_gettable(L, LUA_REGISTRYINDEX);  // tab
  if (lua_isnil(L, -1)) {  // nothing registered
    lua_pop(L, 1);
    return false;
  }

  // Look up key in keytab
  lua_rawgeti(L, -1, 1);  // tab keytab
  lua_pushlstring(L, cud.key.data(), cud.key.size());  // tab keytab key
  lua_rawget(L, -2);  // tab keytab serde
  if (lua_isnil(L, -1)) {  // key not found
    lua_pop(L, 1);
    return false;
  }
  // serde is {mt, serializer, deserializer}

  // Get deserializer (as serde[3])
  lua_rawgeti(L, -1, 3);  // tab keytab value deserializer
  DCHECK(lua_islightuserdata(L, -1));

  auto deserializer = reinterpret_cast<UserDataDeserializer>(
      lua_topointer(L, -1));

  lua_rawgeti(L, -2, 1);  // tab keytab value deserializer mt
  lua_replace(L, -5);  // mt keytab value deserializer
  lua_pop(L, 3);  // mt

  int prevTop = lua_gettop(L);

  // Call deserializer
  deserializer(L, cud.value);
  if (lua_gettop(L) != prevTop + 1) {
    luaL_error(L, "deserializer did not leave one item on the stack");
  }
  luaL_checktype(L, -1, LUA_TUSERDATA);

  // Validate that the metatable of the returned object matches the one
  // we expected.

  if (!lua_getmetatable(L, -1)) {
    luaL_error(L, "deserializer returned object without metatable");
  }
  // expected_mt obj real_mt
  if (!lua_rawequal(L, -1, -3)) {
    luaL_error(L, "deserializer returned object with wrong metatable");
  }
  lua_pop(L, 1);
  lua_remove(L, -2);

  // obj
  return true;
}

}  // namespace

void Deserializer::doDeserializeRefs() {
  lua_pushlightuserdata(L_, this);
  lua_gettable(L_, LUA_REGISTRYINDEX);
  lua_rawgeti(L_, -1, 1);  // converted
  lua_rawgeti(L_, -2, 2);
  int convertedIdx = lua_gettop(L_) - 1;
  int envIdx = lua_isnil(L_, -1) ? 0 : convertedIdx + 1;

  for (int i = 0; i < refs_->size(); ++i) {
#define XLOG DVLOG(XLOG_LEVEL) << "D: reference " << i << ": "
    auto& ref = (*refs_)[i];

    auto record = [&] {
      lua_rawseti(L_, convertedIdx, i + 1);  // 1-based
    };

    if (ref.__isset.stringVal) {
      XLOG << "string [" << ref.stringVal << "]";
      lua_pushlstring(L_, ref.stringVal.data(), ref.stringVal.size());
      record();
    } else if (ref.__isset.tableVal) {
      XLOG << "table";
      lua_newtable(L_);
      record();
    } else if (ref.__isset.functionVal) {
      if (options_ & NO_BYTECODE) {
        luaL_error(L_, "Bytecode deserialization disabled");
      }
      XLOG << "function";
      doDeserializeFunction(ref.functionVal);
      record();
    } else if (ref.__isset.tensorVal) {
      switch (ref.tensorVal.dataType) {
#define DESERIALIZE_TENSOR(TYPE, VALUE) \
      case thpp::ThriftTensorDataType::VALUE: \
        XLOG << "Tensor<" #TYPE ">"; \
        luaPushTensor(L_, thpp::Tensor<TYPE>(ref.tensorVal, mayShare())); \
        break;
      DESERIALIZE_TENSOR(unsigned char, BYTE)
      DESERIALIZE_TENSOR(int32_t, INT32)
      DESERIALIZE_TENSOR(int64_t, INT64)
      DESERIALIZE_TENSOR(float, FLOAT)
      DESERIALIZE_TENSOR(double, DOUBLE)
#undef DESERIALIZE_TENSOR
      default:
        luaL_error(L_, "invalid tensor type");
      }
      record();
    } else if (ref.__isset.storageVal) {
      switch (ref.storageVal.dataType) {
#define DESERIALIZE_STORAGE(TYPE, VALUE) \
      case thpp::ThriftTensorDataType::VALUE: \
        XLOG << "Storage<" #TYPE ">"; \
        luaPushStorage(L_, thpp::Storage<TYPE>(ref.storageVal, mayShare())); \
        break;
      DESERIALIZE_STORAGE(unsigned char, BYTE)
      DESERIALIZE_STORAGE(int32_t, INT32)
      DESERIALIZE_STORAGE(int64_t, INT64)
      DESERIALIZE_STORAGE(float, FLOAT)
      DESERIALIZE_STORAGE(double, DOUBLE)
#undef DESERIALIZE_STORAGE
      default:
        luaL_error(L_, "invalid storage type");
      }
      record();
    } else if (ref.__isset.customUserDataVal) {
      auto& cud = ref.customUserDataVal;
      XLOG << "custom userdata [" << cud.key << "]";
      if (!deserializeUserData(L_, cud)) {
        luaL_error(L_, "Invalid custom userdata");
      }
      record();
    } else if (ref.__isset.envLocation) {
      XLOG << "external env value";
      if (envIdx == 0) {
        luaL_error(L_, "no external env");
      }
      doDeserialize(ref.envLocation.env, convertedIdx, 1, false);
      lua_gettable(L_, envIdx);
      if (lua_isnil(L_, -1)) {
        luaL_error(L_, "expected external env not found");
      }
      doDeserialize(ref.envLocation.key, convertedIdx, 1, false);
      lua_gettable(L_, -2);
      if (lua_isnil(L_, -1)) {
        luaL_error(L_, "expected entry in external env not found");
      }
      lua_remove(L_, -2);
      record();
    } else {
      luaL_error(L_, "Invalid reference");
    }
  }

  for (int i = 0; i < refs_->size(); ++i) {
    auto& ref = (*refs_)[i];

    if (!ref.__isset.tableVal && !ref.__isset.functionVal) {
      continue;
    }

    lua_rawgeti(L_, convertedIdx, i + 1);
    if (ref.__isset.tableVal) {
      XLOG << "table data";
      doSetTable(lua_gettop(L_), convertedIdx, ref.tableVal);
    } else if (ref.__isset.functionVal) {
      XLOG << "function upvalues";
      doSetUpvalues(lua_gettop(L_), convertedIdx, ref.functionVal);
    }
    lua_pop(L_, 1);
  }

  lua_pop(L_, 3);
}

#undef XLOG
#define XLOG DVLOG(XLOG_LEVEL) << "D: " << indent(level)
int Deserializer::doDeserialize(const LuaPrimitiveObject& obj,
                                int convertedIdx, int level, bool allowRefs) {
  if (obj.__isset.refVal) {
    if (!allowRefs || obj.refVal < 0 || obj.refVal >= refs_->size()) {
      luaL_error(L_, "Invalid referernce id %d", int(obj.refVal));
    }
    XLOG << "reference " << obj.refVal;
    lua_rawgeti(L_, convertedIdx, obj.refVal + 1);  // 1-based
    DCHECK_NE(lua_type(L_, -1), LUA_TNIL);
    return 1;
  }

  if (obj.isNil) {
    XLOG << "nil";
    lua_pushnil(L_);
  } else if (obj.__isset.doubleVal) {
    XLOG << "number " << obj.doubleVal;
    lua_pushnumber(L_, obj.doubleVal);
  } else if (obj.__isset.boolVal) {
    XLOG << "boolean " << obj.boolVal;
    lua_pushboolean(L_, obj.boolVal);
  } else if (obj.__isset.stringVal) {
    XLOG << "string [" << obj.stringVal << "]";
    lua_pushlstring(L_, obj.stringVal.data(), obj.stringVal.size());
  } else {
    luaL_error(L_, "Invalid primitive");
  }

  return 1;
}

#undef XLOG
#define XLOG DVLOG(XLOG_LEVEL) << "D:   "

void Deserializer::doSetTable(int index, int convertedIdx,
                              const LuaTable& obj) {
  if (obj.__isset.listKeys) {
    for (int i = 0; i < obj.listKeys.size(); ++i) {
      XLOG << "(list) [" << i + 1 << "]";
      doDeserialize(obj.listKeys[i], convertedIdx, 2);
      lua_rawseti(L_, index, i + 1);
    }
  }
  if (obj.__isset.intKeys) {
    for (auto& p : obj.intKeys) {
      XLOG << "(int) [" << p.first << "]";
      lua_pushinteger(L_, p.first);
      doDeserialize(p.second, convertedIdx, 2);
      lua_rawset(L_, index);
    }
  }
  if (obj.__isset.stringKeys) {
    for (auto& p : obj.stringKeys) {
      XLOG << "(string) [" << p.first << "]";
      lua_pushlstring(L_, p.first.data(), p.first.size());
      doDeserialize(p.second, convertedIdx, 2);
      lua_rawset(L_, index);
    }
  }
  if (obj.__isset.trueKey) {
    XLOG << "(boolean) [true]";
    lua_pushboolean(L_, true);
    doDeserialize(obj.trueKey, convertedIdx, 2);
    lua_rawset(L_, index);
  }
  if (obj.__isset.falseKey) {
    XLOG << "(boolean) [false]";
    lua_pushboolean(L_, false);
    doDeserialize(obj.falseKey, convertedIdx, 2);
    lua_rawset(L_, index);
  }
  if (obj.__isset.otherKeys) {
    for (auto& kv : obj.otherKeys) {
      XLOG << "(other) key";
      doDeserialize(kv.key, convertedIdx, 2);
      XLOG << "(other) value";
      doDeserialize(kv.value, convertedIdx, 2);
      lua_rawset(L_, index);
    }
  }
  if (obj.__isset.metatable) {
    XLOG << "metatable";
    doDeserialize(obj.metatable, convertedIdx, 2);
    lua_setmetatable(L_, index);
  }
  if (obj.__isset.specialKey) {
    XLOG << "special key";
    lua_pushlightuserdata(L_, &kSpecialDeserializationCallbackKey);
    lua_gettable(L_, LUA_REGISTRYINDEX);
    if (lua_isnil(L_, -1)) {
      luaL_error(L_,
                 "Cannot decode special table, no deserialization callback");
    }
    doDeserialize(obj.specialKey, convertedIdx, 2);
    if (obj.__isset.specialValue) {
      XLOG << "special value";
      doDeserialize(obj.specialValue, convertedIdx, 2);
    } else {
      lua_pushnil(L_);
    }
    lua_pushvalue(L_, index);
    lua_call(L_, 3, 0);
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

void Deserializer::doDeserializeFunction(const LuaFunction& obj) {
  folly::io::Cursor cursor(&obj.bytecode);
  int r = lua_load(L_, luaReaderFromIOBuf, &cursor, "<thrift>");
  if (r != 0) {
    luaL_error(L_, "lua_load error %d", r);
  }
}

void Deserializer::doSetUpvalues(int idx, int convertedIdx,
                                 const LuaFunction& obj) {
  for (int i = 0; i < obj.upvalues.size(); ++i) {
    doDeserialize(obj.upvalues[i], convertedIdx, 2);
    auto r = lua_setupvalue(L_, idx, i + 1);
    if (!r) {
      luaL_error(L_, "too many upvalues");
    }
  }
}

}}  // namespaces
