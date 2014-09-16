/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// C++ interface with Thrift-serialized Lua objects, without needing to
// call into (or even link with) Lua

#ifndef FBLUA_THRIFT_LUAOBJECT_H_
#define FBLUA_THRIFT_LUAOBJECT_H_

#include <fblualib/thrift/Encoding.h>
#include <fblualib/thrift/if/gen-cpp2/LuaObject_types.h>
#include <folly/Range.h>
#include <thpp/Tensor.h>

namespace fblualib { namespace thrift {

enum class LuaObjectType {
  NIL,
  DOUBLE,
  BOOL,
  STRING,
  TABLE,
  FUNCTION,
  TENSOR,
  STORAGE,
};

// Readers

LuaObjectType getType(const LuaObject& obj);
bool isNil(const LuaObject& obj);
bool asBool(const LuaObject& obj);  // convert to bool by Lua rules
double getDouble(const LuaObject& obj);
bool getBool(const LuaObject& obj);
folly::StringPiece getString(const LuaObject& obj);
thpp::ThriftTensorDataType getTensorType(const LuaObject& obj);
// Note, rvalue overload only, as getTensor is destructive.
template <class T> thpp::Tensor<T> getTensor(LuaObject&& obj);

// Writers

LuaObject make();  // nil
LuaObject make(double val);
LuaObject make(bool val);
LuaObject make(folly::StringPiece val);
// Note, non-const reference; the tensor shares memory with the given LuaObject
// and so any changes in the tensor are reflected in the LuaObject
template <class T>
LuaObject make(thpp::Tensor<T>& val);
template <class T>
LuaObject make(thpp::Tensor<T>&& val) {
  return make<T>(val);
}

// Serialize to string or file, see Encoding.h
template <class Writer>
void cppEncode(const LuaObject& input, folly::io::CodecType codec,
               Writer&& writer);

// Deserialize from string or file, see Encoding.h
template <class Reader>
LuaObject cppDecode(Reader&& reader);

}}  // namespaces

#include <fblualib/thrift/LuaObject-inl.h>

#endif /* FBLUA_THRIFT_LUAOBJECT_H_ */
