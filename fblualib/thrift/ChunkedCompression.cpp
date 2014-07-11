/**
 * Copyright 2014 Facebook
 * @author Tudor Bosman (tudorb@fb.com)
 */

#include <fblualib/thrift/ChunkedCompression.h>

namespace fblualib { namespace thrift {

std::unique_ptr<folly::IOBuf> compressChunked(
    folly::io::Codec* codec,
    const folly::IOBuf* uncompressed,
    uint64_t chunkLength,
    ChunkList& chunks) {
  folly::io::Cursor cursor(uncompressed);
  folly::IOBufQueue compressed(folly::IOBufQueue::cacheChainLength());
  uint64_t compressedLength = 0;

  for (;;) {
    std::unique_ptr<folly::IOBuf> uncompressedChunk;
    size_t n = cursor.cloneAtMost(uncompressedChunk, chunkLength);
    if (n == 0) {
      break;
    }

    Chunk chunk;
    chunk.uncompressedLength = n;
    compressed.append(codec->compress(uncompressedChunk.get()));

    // Don't walk the IOBuf chain twice, let IOBufQueue::append do the
    // job, we'll compute the current length as the difference of
    // (queue new length) - (queue old length)
    auto newLength = compressed.chainLength();
    chunk.compressedLength = newLength - compressedLength;
    compressedLength = newLength;

    chunks.chunks.push_back(std::move(chunk));
  }

  return compressed.move();
}

std::unique_ptr<folly::IOBuf> uncompressChunked(
    folly::io::Codec* codec,
    const folly::IOBuf* compressed,
    const ChunkList& chunks) {
  folly::io::Cursor cursor(compressed);
  folly::IOBufQueue uncompressed(folly::IOBufQueue::cacheChainLength());
  uint64_t uncompressedLength = 0;

  for (auto& chunk : chunks.chunks) {
    std::unique_ptr<folly::IOBuf> compressedChunk;
    size_t n = cursor.cloneAtMost(compressedChunk, chunk.compressedLength);
    if (n != chunk.compressedLength) {
      throw std::runtime_error("underflow");
    }

    uncompressed.append(codec->uncompress(compressedChunk.get(),
                                          chunk.uncompressedLength));

    // Don't walk the IOBuf chain twice, let IOBufQueue::append do the
    // job, we'll compute the current length as the difference of
    // (queue new length) - (queue old length)
    auto newLength = uncompressed.chainLength();
    if (newLength - uncompressedLength != chunk.uncompressedLength) {
      throw std::runtime_error("decompression error");
    }
    uncompressedLength = newLength;
  }

  return uncompressed.move();
}

}}  // namespaces
