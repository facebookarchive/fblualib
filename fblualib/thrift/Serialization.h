/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_THRIFT_SERIALIZATION_H_
#define FBLUA_THRIFT_SERIALIZATION_H_

#include <memory>
#include <unordered_map>

#include <lua.hpp>

#include <folly/io/IOBuf.h>
#include <fblualib/thrift/if/gen-cpp2/LuaObject_types.h>

namespace fblualib { namespace thrift {

// Set the Lua function at the given stack index as the "special serialization
// callback". This is called for all tables that have a metatable in order to
// serialize OO types differently -- for example, to serialize unique
// identifiers (type names) instead of method implementations.
//
// Note that the default behavior works for most purposes; objects in Lua are
// just tables, and their classes (set as metatables) are tables too, so the
// default serialization mechanism would work in most cases. It would, however,
// serialize method implementations as bytecode, which may not be what you want
// (it would be somewhat surprising if, for example, loading a config file
// would revert your object implementation to the one in use when the config
// file was written). Also, we can't serialize methods implemented in C
// (we raise an error in that case).
//
// The API is:
//
// special_key, special_val, table, metatable = callback(table)
//
// special_key: if not nil, key to pass to the deserialization callback
// special_val: value to pass to the deserialization callback
// table:       if nil, serialize current object;
//              otherwise, serialize given table instead
// metatable:   if nil, serialize current metatable;
//              if false, serialize NO metatable;
//              otherwise, serialize given metatable instead
//
// (The distinction between special_key and special_val is arbitrary;
// the Lua component of this library uses special_key to distinguish
// between multiple OOP schemes, and special_val as a type name if
// appropriate)
//
// For example, most OOP implementations use the metatable as the object's
// "class"; the implementation is functions in the metatable, and you don't
// want to serialize their bytecode.
//
// So your callback would, for each object with a matching metatable,
// return (unique_id_for_this_metatable, '', nil, false)
//
// Torch serialization goes one level further and maintains a global map
// of type names -> metatable (so a class is uniquely identified by its
// typename). In that case, your callback could return, for each torch
// objects, ('torch', type_name, nil, false)
void setSpecialSerializationCallback(lua_State* L, int index);

// Set the Lua function at the given stack index as the "special
// deserialization callback". This is called for all serialized tables that
// were serialized with a special callback (that returned a non-nil
// special_key)
//
// The API is:
//
// callback(special_key, special_val, table)
//
// table isinitialized with the deserialized value (and the metatable is set
// appropriately); the callback must mutate table in place.
//
// For most OOP implementations, this only consists of a setmetatable call
// to set the object's class appropriately. The callback would find the
// appropriate metatable based on special_key and special_val.
//
// For the torch example above, the callback could be:
//
// local function deserialize_cb(key, val, obj)
//     if key == 'torch' then
//         setmetatable(obj, torch.getmetatable(val))
//     end
// end
void setSpecialDeserializationCallback(lua_State* L, int index);

// Serialize a Lua object to thrift, returning the result either as a Thrift
// object or as raw encoded bytes (using CompactProtocol).
class Serializer {
 public:
  LuaObject toThrift(lua_State* L, int index);

 private:
  void setMinVersion(int v);
  void doSerialize(LuaPrimitiveObject& obj, lua_State* L, int index, int level);
  void doSerializeTable(LuaTable& obj, lua_State* L, int index, int level);
  void doSerializeFunction(LuaFunction& obj, lua_State* L, int index,
                           int level);

  LuaObject out_;
  std::unordered_map<const void*, int64_t> converted_;
};

// Deserialize a Lua object, from a Thrift object or from raw bytes
// (*without* length prepended)
class Deserializer {
 public:
  enum : unsigned int {
    NO_BYTECODE = 1U << 0,
  };
  explicit Deserializer(unsigned int options=0) : options_(options) { }

  int fromThrift(lua_State* L, LuaObject&& obj);

 private:
  void doDeserializeRefs(lua_State* L);
  int doDeserialize(lua_State* L, LuaPrimitiveObject& obj, int level);
  void doDeserializeFunction(lua_State* L, LuaFunction& obj);
  void doSetTable(lua_State* L, int index, LuaTable& obj);
  void doSetUpvalues(lua_State* L, int index, LuaFunction& obj);

  unsigned int options_;
  // Cache elements that we've already seen; map from refid (in the
  // thrift object) to position in the converted Lua table (which is itself
  // at convertedIdx_ on the Lua stack)
  LuaObject in_;
  int convertedIdx_ = 0;
};

}}  // namespaces

#endif /* FBLUA_THRIFT_SERIALIZATION_H_ */
