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

#include <boost/iterator.hpp>
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
  EXTERNAL,  // not encoded -- assumed to exist in environment
  USERDATA,  // (custom) userdata (not tensor / storage)
};

// Readers

LuaObjectType getType(const LuaPrimitiveObject& pobj,
                      const LuaRefList& refs = LuaRefList());
inline LuaObjectType getType(const LuaObject& obj) {
  return getType(obj.value, obj.refs);
}
bool isNil(const LuaPrimitiveObject& pobj);
inline bool isNil(const LuaObject& obj) {
  return isNil(obj.value);
}
bool asBool(const LuaPrimitiveObject& pobj);
inline bool asBool(const LuaObject& obj) {  // convert to bool by Lua rules
  return asBool(obj.value);
}
double getDouble(const LuaPrimitiveObject& pobj);
inline double getDouble(const LuaObject& obj) {
  return getDouble(obj.value);
}
bool getBool(const LuaPrimitiveObject& pobj);
inline bool getBool(const LuaObject& obj) {
  return getBool(obj.value);
}
folly::StringPiece getString(const LuaPrimitiveObject& pobj,
                             const LuaRefList& refs);
inline folly::StringPiece getString(const LuaObject& obj) {
  return getString(obj.value, obj.refs);
}
thpp::ThriftTensorDataType getTensorType(const LuaPrimitiveObject& pobj,
                                         const LuaRefList& refs);
inline thpp::ThriftTensorDataType getTensorType(const LuaObject& obj) {
  return getTensorType(obj.value, obj.refs);
}
template <class T>
thpp::TensorPtr<thpp::Tensor<T>> getTensor(
    const LuaPrimitiveObject& pobj,
    const LuaRefList& refs,
    thpp::SharingMode sharing = thpp::SHARE_IOBUF_MANAGED);
template <class T> inline typename thpp::Tensor<T>::Ptr getTensor(
    const LuaObject& obj,
    thpp::SharingMode sharing = thpp::SHARE_IOBUF_MANAGED) {
  return getTensor<T>(obj.value, obj.refs, sharing);
}

// Table access
bool isList(const LuaPrimitiveObject& obj, const LuaRefList& refs);
inline bool isList(const LuaObject& obj) {
  return isList(obj.value, obj.refs);
}
size_t listSize(const LuaPrimitiveObject& obj, const LuaRefList& refs);
inline bool listSize(const LuaObject& obj) {
  return listSize(obj.value, obj.refs);
}

// Iterator that iterates through all elements in an unspecified order
// (like Lua's pairs()). Create with tableBegin() and tableEnd(), below.
class TableIterator : public boost::iterator_facade<
    TableIterator,
    const std::pair<LuaPrimitiveObject, LuaPrimitiveObject>,
    boost::forward_traversal_tag> {
  friend class boost::iterator_core_access;
 public:
  explicit TableIterator(const LuaTable& table);
  TableIterator();

 private:
  const std::pair<LuaPrimitiveObject, LuaPrimitiveObject>&
    dereference() const;
  bool equal(const TableIterator& other) const;
  void increment();
  void skipEmpty();

  enum State {
    LIST_KEYS,
    STRING_KEYS,
    INT_KEYS,
    TRUE_KEY,
    FALSE_KEY,
    OTHER_KEYS,
    END,
  };

  const LuaTable* table_;
  State state_;
  // Yes, this could be a C++11 union, but I'm lazy.
  size_t index_;
  std::unordered_map<std::string, LuaPrimitiveObject>::const_iterator
    stringKeysIterator_;
  std::unordered_map<int64_t, LuaPrimitiveObject>::const_iterator
    intKeysIterator_;

  mutable bool dirty_;
  mutable std::pair<LuaPrimitiveObject, LuaPrimitiveObject> value_;
};

TableIterator tableBegin(const LuaPrimitiveObject& pobj,
                         const LuaRefList& refs);

inline TableIterator tableBegin(const LuaObject& obj) {
  return tableBegin(obj.value, obj.refs);
}

inline TableIterator tableEnd(const LuaPrimitiveObject& obj,
                              const LuaRefList& refs) {
  return TableIterator();
}
inline TableIterator tableEnd(const LuaObject& obj) {
  return tableEnd(obj.value, obj.refs);
}

// Iterator that iterates through the list-like part of the table
// (like Lua's ipairs()). Create with listBegin() and listEnd(), below.
typedef std::vector<LuaPrimitiveObject>::const_iterator ListIterator;

ListIterator listBegin(const LuaPrimitiveObject& pobj,
                       const LuaRefList& refs);
inline ListIterator listBegin(const LuaObject& obj) {
  return listBegin(obj.value, obj.refs);
}

ListIterator listEnd(const LuaPrimitiveObject& pobj,
                     const LuaRefList& refs);
inline ListIterator listEnd(const LuaObject& obj) {
  return listEnd(obj.value, obj.refs);
}

// Writers

LuaPrimitiveObject makePrimitive();  // nil
inline LuaObject make() {
  LuaObject r;
  r.value = makePrimitive();
  return r;
}
LuaPrimitiveObject makePrimitive(double val);
inline LuaObject make(double val) {
  LuaObject r;
  r.value = makePrimitive(val);
  return r;
}
LuaPrimitiveObject makePrimitive(bool val);
inline LuaObject make(bool val) {
  LuaObject r;
  r.value = makePrimitive(val);
  return r;
}
LuaPrimitiveObject makePrimitive(folly::StringPiece val);
inline LuaObject make(folly::StringPiece val) {
  LuaObject r;
  r.value = makePrimitive(val);
  return r;
}
template <class T>
LuaPrimitiveObject append(
    const thpp::Tensor<T>& val, LuaRefList& refs,
    thpp::SharingMode sharing = thpp::SHARE_IOBUF_MANAGED);
template <class T>
inline LuaObject make(const thpp::Tensor<T>& val,
                      thpp::SharingMode sharing = thpp::SHARE_IOBUF_MANAGED) {
  LuaObject r;
  r.value = append(val, r.refs, sharing);
  return r;
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
