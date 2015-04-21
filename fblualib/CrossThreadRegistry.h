/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>

#include <folly/MapUtil.h>

namespace {

template<typename Key, typename Val>
class CrossThreadRegistry {
  std::mutex m_mutex;
  typedef std::unordered_map<Key, std::unique_ptr<Val>> Registry;
  Registry  m_registry;

public:
  template <typename Lambda>
  Val* getOrCreate(const Key& key, Lambda factory) {
    std::lock_guard<std::mutex> l(m_mutex);
    auto pos = m_registry.find(key);
    if (pos == m_registry.end()) {
      pos = m_registry.emplace(key, factory()).first;
    }
    return pos->second.get();
  }

  template<typename Lambda>
  bool create(const Key& key, Lambda factory) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (folly::get_ptr(m_registry, key)) {
      return false;
    }
    m_registry[key] = factory();
    return true;
  }

  bool erase(const Key& key) {
    std::lock_guard<std::mutex> l(m_mutex);
    return m_registry.erase(key);
  }

  Val* get(const Key& key) {
    std::lock_guard<std::mutex> l(m_mutex);
    if (auto valp = folly::get_ptr(m_registry, key)) {
      return valp->get();
    }
    return nullptr;
  }
};

}
