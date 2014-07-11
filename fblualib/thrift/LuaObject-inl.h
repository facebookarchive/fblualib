/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#ifndef FBLUA_THRIFT_LUAOBJECT_H_
#error This file may only be included from fblualib/thrift/LuaObject.h
#endif

namespace fblualib { namespace thrift {

namespace detail {
LuaVersionInfo cppVersionInfo();
}  // namespace detail

template <class Writer>
void cppEncode(const LuaObject& input, folly::io::CodecType codec,
               Writer&& writer) {
  return encode(input, codec, detail::cppVersionInfo(),
                std::forward<Writer>(writer));
}

template <class Reader>
LuaObject cppDecode(Reader&& reader) {
  return decode(std::forward<Reader>(reader)).output;
}

}}  // namespaces
