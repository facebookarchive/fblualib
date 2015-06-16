/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_THRIFT_ENCODING_H_
#define FBLUA_THRIFT_ENCODING_H_

#include <folly/io/Compression.h>
#include <folly/io/IOBuf.h>
#include <fblualib/thrift/if/gen-cpp2/LuaObject_types.h>

namespace fblualib { namespace thrift {

// void writer(std::unique_ptr<folly::IOBuf> data);
constexpr int kAnyVersion = std::numeric_limits<int>::max();

template <class Writer>
void encode(const LuaObject& input, folly::io::CodecType codec,
            LuaVersionInfo versionInfo, Writer&& writer,
            int maxVersion = kAnyVersion,
            uint64_t chunkLength = std::numeric_limits<uint64_t>::max());

struct DecodedObject {
  LuaObject output;
  LuaVersionInfo luaVersionInfo;
};

// std::unique_ptr<folly::IOBuf> reader(size_t n);
template <class Reader>
DecodedObject decode(Reader&& reader);

class FILEWriter {
 public:
  explicit FILEWriter(FILE* fp) : fp_(fp) { }

  void operator()(std::unique_ptr<folly::IOBuf> data);
 private:
  FILE* fp_;
};

class FILEReader {
 public:
  explicit FILEReader(FILE* fp) : fp_(fp) { }

  std::unique_ptr<folly::IOBuf> operator()(size_t n);
 private:
  FILE* fp_;
};

class StringWriter {
 public:
  folly::ByteRange finish();
  void operator()(std::unique_ptr<folly::IOBuf> data);

 private:
  folly::IOBuf buf_;
};

class StringReader {
 public:
  // Note that str must outlive the decoded object, as the IOBufs inside
  // DecodedObject will end up pointing to str (but will be marked as shared).
  explicit StringReader(folly::ByteRange* str) : str_(str) { }

  std::unique_ptr<folly::IOBuf> operator()(size_t n);

 private:
  folly::ByteRange* str_;
};

}}  // namespaces

#endif /* FBLUA_THRIFT_ENCODING_H_ */
