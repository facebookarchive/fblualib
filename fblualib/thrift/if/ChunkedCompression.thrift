//
//  Copyright (c) 2014, Facebook, Inc.
//  All rights reserved.
//
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//

namespace cpp2 fblualib.thrift

struct Chunk {
  1: i64 compressedLength,
  2: i64 uncompressedLength,
}

struct ChunkList {
  1: list<Chunk> chunks;
}
