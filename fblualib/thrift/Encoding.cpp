/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "Encoding.h"

#include <folly/Bits.h>
#include <folly/Exception.h>
#include <folly/Format.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <fblualib/thrift/ChunkedCompression.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace fblualib { namespace thrift {

namespace {

constexpr uint32_t kMagic = 0x5441554c;  // "LUAT", little-endian
constexpr int kMaxSupportedVersion = 3;

FOLLY_PACK_PUSH
struct Header {
  // All values little-endian
  uint32_t magic;         // kMagic
  uint32_t thriftHeaderLength;  // length of Thrift header
} FOLLY_PACK_ATTR;
FOLLY_PACK_POP

}  // namespace

template <class Writer>
void encode(const LuaObject& input, folly::io::CodecType codecType,
            LuaVersionInfo versionInfo, Writer&& writer, int maxVersion,
            uint64_t chunkLength) {
  folly::IOBufQueue dataQueue(folly::IOBufQueue::cacheChainLength());
  apache::thrift::CompactSerializer::serialize(input, &dataQueue);
  auto codec = folly::io::getCodec(codecType);

  // Determine minimum version required for reading
  bool needChunking = false;
  uint64_t codecMaxLength = codec->maxUncompressedLength();
  if (codecMaxLength < chunkLength) {
    chunkLength = codecMaxLength;
  }

  int version = 0;

  if (dataQueue.chainLength() > chunkLength) {
    needChunking = true;
    // Version 2: chunking
    version = 2;
  }

  for (auto& ref : input.refs) {
    if (ref.__isset.envLocation) {
      // Version 3: external env / package references
      version = 3;
      break;
    }
    if (version < 1 && ref.__isset.tableVal) {
      auto& table = ref.tableVal;
      if (table.__isset.specialKey ||
          table.__isset.specialValue ||
          table.__isset.metatable) {
        // Version 1: specials, metatables
        version = 1;
      }
    }
  }

  DCHECK_LE(version, kMaxSupportedVersion);

  if (version > maxVersion) {
    throw std::invalid_argument(folly::to<std::string>(
        "Version ", version, " required (requested ", maxVersion, ")"));
  }

  ThriftHeader th;
  th.version = version;
  th.codec = static_cast<int32_t>(codecType);
  th.uncompressedLength = dataQueue.chainLength();
  th.luaVersionInfo = std::move(versionInfo);

  auto uncompressed = dataQueue.move();
  std::unique_ptr<folly::IOBuf> compressed;
  if (needChunking) {
    th.__isset.chunks = true;
    compressed = compressChunked(
        codec.get(), uncompressed.get(), chunkLength,
        th.chunks);
  } else {
    compressed = codec->compress(uncompressed.get());
  }
  th.compressedLength = compressed->computeChainDataLength();

  folly::IOBufQueue queue(folly::IOBufQueue::cacheChainLength());
  apache::thrift::CompactSerializer::serialize(th, &queue);

  Header header;
  header.magic = folly::Endian::little(kMagic);
  header.thriftHeaderLength = folly::Endian::little(queue.chainLength());

  writer(folly::IOBuf::copyBuffer(&header, sizeof(header)));
  writer(queue.move());
  writer(std::move(compressed));
}

#define X(T) \
template void encode(const LuaObject& input, \
                     folly::io::CodecType codecType, \
                     LuaVersionInfo info, \
                     T& writer, \
                     int minVersion, \
                     uint64_t chunkLength);
X(StringWriter)
X(FILEWriter)
#undef X

template <class Reader>
DecodedObject decode(Reader&& reader) {
  auto headerBuf = reader(sizeof(Header));
  auto header = reinterpret_cast<const Header*>(headerBuf->data());

  auto magic = folly::Endian::little(header->magic);
  auto thriftHeaderLength = folly::Endian::little(header->thriftHeaderLength);
  if (magic != kMagic) {
    throw std::runtime_error(
        folly::sformat("bad magic {:x}, expected {:x}", magic, kMagic));
  }
  auto thriftHeaderBuf = reader(thriftHeaderLength);
  ThriftHeader th;
  apache::thrift::CompactSerializer::deserialize(thriftHeaderBuf.get(), th);

  if (th.version > kMaxSupportedVersion) {
    throw std::runtime_error(folly::sformat("bad version {}", th.version));
  }

  auto codec = folly::io::getCodec(static_cast<folly::io::CodecType>(th.codec));
  auto compressedBuf = reader(th.compressedLength);

  DecodedObject decodedObject;
  std::unique_ptr<folly::IOBuf> buf;

  if (th.__isset.chunks) {
    buf = uncompressChunked(codec.get(), compressedBuf.get(), th.chunks);
  } else {
    buf = codec->uncompress(compressedBuf.get(), th.uncompressedLength);
  }
  apache::thrift::CompactSerializer::deserialize(buf.get(),
                                                 decodedObject.output);

  decodedObject.luaVersionInfo = std::move(th.luaVersionInfo);
  return decodedObject;
}

#define X(T) \
template DecodedObject decode(T& reader);
X(StringReader)
X(FILEReader)
#undef X

void FILEWriter::operator()(std::unique_ptr<folly::IOBuf> data) {
  for (; data; data = data->pop()) {
    if (data->length() == 0) {
      continue;
    }
    size_t bytesWritten = fwrite(data->data(), 1, data->length(), fp_);
    if (bytesWritten < data->length()) {
      folly::throwSystemError("FILEWriter: fwrite");
    }
  }
}

std::unique_ptr<folly::IOBuf> FILEReader::operator()(size_t n) {
  auto buf = folly::IOBuf::create(n);
  size_t bytesRead = fread(buf->writableData(), 1, n, fp_);
  if (bytesRead < n) {
    folly::throwSystemError("FILEReader: fread");
  }
  buf->append(n);
  return buf;
}

void StringWriter::operator()(std::unique_ptr<folly::IOBuf> data) {
  buf_.prependChain(std::move(data));
}

folly::ByteRange StringWriter::finish() {
  return buf_.coalesce();
}

std::unique_ptr<folly::IOBuf> StringReader::operator()(size_t n) {
  auto buf = folly::IOBuf::wrapBuffer(str_->subpiece(0, n));
  str_->advance(n);
  return buf;
}

}}  // namespaces
