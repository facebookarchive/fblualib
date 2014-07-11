/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUALIB_THRIFT_CHUNKEDCOMPRESSION_H_
#define FBLUALIB_THRIFT_CHUNKEDCOMPRESSION_H_

#include <memory>
#include <vector>

#include <folly/io/IOBuf.h>
#include <folly/io/Compression.h>

#include <fblualib/thrift/if/gen-cpp2/ChunkedCompression_types.h>

namespace fblualib { namespace thrift {

// Some compression implementations (such as LZ4 and Snappy) have a maximum size
// of a compressed object (the limit is 1.9GiB for LZ4 and 4GiB for Snappy).
//
// If we want to compress objects larger than the limit, we use a
// chunked scheme where we break down the object into chunks smaller
// than the limit and compress them independently.
//
// Note that the chunks are compressed as separate compressed objects,
// not as part of the same stream; we wouldn't need to use chunking if
// the compression implementation supported streams of unlimited length.

std::unique_ptr<folly::IOBuf> compressChunked(
    folly::io::Codec* codec,
    const folly::IOBuf* uncompressed,
    uint64_t chunkLength,
    ChunkList& chunks);

std::unique_ptr<folly::IOBuf> uncompressChunked(
    folly::io::Codec* codec,
    const folly::IOBuf* compressed,
    const ChunkList& chunks);

}}  // namespaces

#endif /* FBLUALIB_THRIFT_CHUNKEDCOMPRESSION_H_ */
