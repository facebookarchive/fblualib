/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef NDEBUG

#include "Debug.h"

#include <mutex>
#include <unordered_map>

#include <glog/logging.h>

namespace fblualib {
namespace python {

namespace {

class RefMap {
 public:
  typedef std::unordered_map<const void*, int> MapType;

  void add(const void* obj);
  void remove(const void* obj);
  void check(const void* obj) const;
  void checkEqual(const MapType& other) const;
  void copyTo(MapType& map);

 private:
  std::unordered_map<const void*, int> map_;
  mutable std::mutex mutex_;
};

void RefMap::add(const void* obj) {
  CHECK(obj);
  std::lock_guard<std::mutex> lock(mutex_);
  ++map_[obj];
}

void RefMap::remove(const void* obj) {
  CHECK(obj);
  std::lock_guard<std::mutex> lock(mutex_);
  auto pos = map_.find(obj);
  CHECK(pos != map_.end());
  if (--pos->second <= 0) {
    CHECK_EQ(pos->second, 0);
    map_.erase(pos);
  }
}

void RefMap::check(const void* obj) const {
  CHECK(obj);
  std::lock_guard<std::mutex> lock(mutex_);
  auto pos = map_.find(obj);
  CHECK(pos != map_.end());
  CHECK_GT(pos->second, 0);
}

void RefMap::checkEqual(const MapType& other) const {
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(map_ == other);
}

void RefMap::copyTo(MapType& map) {
  std::lock_guard<std::mutex> lock(mutex_);
  map = map_;
}

RefMap luaRefs;
RefMap pythonRefs;

RefMap::MapType watermarkLuaRefs;
RefMap::MapType watermarkPythonRefs;

}  // namespace

void debugAddLuaRef(const void* obj) { luaRefs.add(obj); }
void debugDeleteLuaRef(const void* obj) { luaRefs.remove(obj); }
void debugCheckLuaRef(const void* obj) { luaRefs.check(obj); }
void debugCheckNoLuaRefs() { luaRefs.checkEqual(watermarkLuaRefs); }

void debugAddPythonRef(const PyObject* obj) { pythonRefs.add(obj); }
void debugDeletePythonRef(const PyObject* obj) { pythonRefs.remove(obj); }
void debugCheckPythonRef(const PyObject* obj) { pythonRefs.check(obj); }
void debugCheckNoPythonRefs() { pythonRefs.checkEqual(watermarkPythonRefs); }

void debugSetWatermark() {
  luaRefs.copyTo(watermarkLuaRefs);
  pythonRefs.copyTo(watermarkPythonRefs);
}

void debugCheckNoRefs() {
  debugCheckNoLuaRefs();
  debugCheckNoPythonRefs();
}

}}  // namespaces

#endif  /* !NDEBUG */
