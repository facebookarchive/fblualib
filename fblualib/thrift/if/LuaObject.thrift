//
//  Copyright (c) 2014, Facebook, Inc.
//  All rights reserved.
//
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
//
// Thrift serialization for arbitrary Lua objects.
//
// We support primitive types (nil, number, boolean, string) and
// reference types (string, table, function, userdata). The only
// supported userdata is torch tensors. Note that string may be either
// a primitive type or a reference type, depending on whether it's interned
// or not.
//
// We store all objects of reference types in LuaObject.refs. LuaPrimitive
// represents an object of a primitive type, or the index of a reference
// in LuaObject.refs.
//
// We try to make this useful from C/C++ by separating table elements
// according to their keys: list-like (consecutive integers starting at 1; note
// that the indexes in LuaTable.listKeys start at 0!), other integral keys,
// string keys, boolean keys, and "other". So a list would only have
// listKeys set, a string-keyed dictionary would only have stringKeys set, etc.
// Of course, an arbitrary Lua table may have more than one such field set,
// but that's unusual.
//
// We store functions (as bytecode) and also serialize function upvalues.
// Bytecode is not guaranteed to be compatible across Lua versions, and
// we check for this when deserializing. (For LuaJIT, versions with the same
// major and minor version number are bytecode-compatible; that is, LuaJIT
// 2.0.2 is compatible (forwards and backwards) with LuaJIT 2.0.x, but not
// LuaJIT 2.1.x or LuaJIT 3.x.y)

namespace cpp2 fblualib.thrift

include "thpp/if/Tensor.thrift"
include "fblualib/thrift/if/ChunkedCompression.thrift"

typedef binary (cpp2.type = "folly::IOBuf") IOBuf

struct LuaPrimitiveObject {
  1: bool isNil,
  2: optional double doubleVal,
  3: optional bool boolVal,
  4: optional binary stringVal,
  5: optional i64 refVal,
}

struct LuaPrimitiveObjectKV {
  1: LuaPrimitiveObject key,
  2: LuaPrimitiveObject value,
}

struct LuaTable {
  1: optional list<LuaPrimitiveObject> listKeys,
  2: optional hash_map<binary, LuaPrimitiveObject> stringKeys,
  3: optional hash_map<i64, LuaPrimitiveObject> intKeys,
  4: optional LuaPrimitiveObject trueKey,
  5: optional LuaPrimitiveObject falseKey,
  6: optional list<LuaPrimitiveObjectKV> otherKeys,
  7: optional LuaPrimitiveObject specialKey,
  8: optional LuaPrimitiveObject specialValue,
  9: optional LuaPrimitiveObject metatable,
}

struct LuaFunction {
  1: IOBuf bytecode,
  2: list<LuaPrimitiveObject> upvalues,
}

struct LuaExternalEnvLocation {
  1: required LuaPrimitiveObject env,  // may not be reference
  2: required LuaPrimitiveObject key,  // may not be reference
}

struct LuaUserData {
  1: required string key,
  2: required IOBuf value,
}

struct LuaRefObject {
  1: optional binary stringVal,
  2: optional LuaTable tableVal,
  3: optional LuaFunction functionVal,
  4: optional Tensor.ThriftTensor tensorVal,
  5: optional Tensor.ThriftStorage storageVal,
  6: optional LuaExternalEnvLocation envLocation,
  7: optional LuaUserData customUserDataVal,
}

typedef list<LuaRefObject> LuaRefList

// A Lua object.
struct LuaObject {
  1: LuaPrimitiveObject value,
  2: LuaRefList refs,
}

struct LuaVersionInfo {
  // Encodings with different bytecodeVersion can't deserialize
  // functions (bytecode)
  1: string bytecodeVersion,
  2: string interpreterVersion,
}

struct ThriftHeader {
  // 0 = initial version
  // 1 = support for metatables, specials
  // 2 = support for chunked encoding
  // 3 = support for external environments
  // 4 = support for custom userdata
  1: i32 version,
  2: i32 codec,
  3: i64 uncompressedLength,
  4: i64 compressedLength,
  5: LuaVersionInfo luaVersionInfo,
  6: optional ChunkedCompression.ChunkList chunks,
}
