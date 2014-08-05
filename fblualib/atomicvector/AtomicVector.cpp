/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "AtomicVector.h"

#include <lua.hpp>
#include <fblualib/LuaUtils.h>

using namespace fblualib;
using namespace fblualib::detail;
using namespace std;

// Teach AtomicVector how to refcount our tensors.
template<> struct Refcount<THFloatTensor*> {
  void inc(THFloatTensor* t) {
    THFloatTensor_retain(t);
  }

  void dec(THFloatTensor* t) {
    THFloatTensor_free(t);
  }
};

template<> struct Refcount<THDoubleTensor*> {
  void inc(THDoubleTensor* t) {
    THDoubleTensor_retain(t);
  }

  void dec(THDoubleTensor* t) {
    THDoubleTensor_free(t);
  }
};

namespace {

constexpr const char* kTypeName = "fblualib.atomicvector";

struct TorchAtomicVectorIf {
  typedef int BucketIndex;
  virtual ~TorchAtomicVectorIf() { }

  virtual int luaRead(lua_State* L) = 0;
  virtual int luaWrite(lua_State* L) = 0;
  virtual int luaAppend(lua_State* L) = 0;
  virtual int luaSize(lua_State* L) = 0;
};

TorchAtomicVectorIf*
checkAtomicVec(lua_State* L, int idx) {
  auto av = static_cast<TorchAtomicVectorIf**>
    (luaL_checkudata(L, idx, kTypeName));
  DCHECK(av);
  return *av;
}

template<typename Real>
class TorchAtomicVector : public TorchAtomicVectorIf {
  AtomicVector<typename thpp::Tensor<Real>::THType*> m_av;
  typedef typename thpp::Tensor<Real>::THType Tensor;

  virtual Tensor*
  checkTensor(lua_State* L, int idx) {
    auto t = static_cast<Tensor**>(
      luaL_checkudata(L, idx, kTensorTypeName));
    DCHECK(t);
    return *t;
  }

 public:
  constexpr static const char* kTensorTypeName =
    thpp::Tensor<Real>::kLuaTypeName;

  virtual ~TorchAtomicVector() { }

  virtual int luaRead(lua_State* L) {
    int idx = luaL_checknumber(L, 2);
    auto val = m_av.read(idx - 1);
    luaT_pushudata(L, val, kTensorTypeName);
    return 1;
  }

  virtual int luaWrite(lua_State* L) {
    int idx = luaL_checknumber(L, 2);
    auto val = checkTensor(L, 3);
    m_av.write(idx - 1, val);
    return 0;
  }

  virtual int luaAppend(lua_State* L) {
    auto t = checkTensor(L, 2);
    m_av.append(t);
    return 0;
  }

  virtual int luaSize(lua_State* L) {
    lua_pushnumber(L, m_av.size());
    return 1;
  }
};

typedef unordered_map<string, unique_ptr<TorchAtomicVectorIf>> VecTab;
static VecTab g_vecTab;
static mutex g_vecTabLock;

template<typename Real>
int create(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  unique_lock<mutex> l(g_vecTabLock);
  auto row = g_vecTab.find(name);
  if (row == g_vecTab.end()) {
    g_vecTab[name] = unique_ptr<TorchAtomicVectorIf>
      (new TorchAtomicVector<Real>());
    lua_pushboolean(L, true);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int createDouble(lua_State* L) {
  return create<double>(L);
}

int createFloat(lua_State* L) {
  return create<float>(L);
}

int destroy(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  unique_lock<mutex> l(g_vecTabLock);
  auto row = g_vecTab.find(name);
  if (row != g_vecTab.end()) {
    g_vecTab.erase(row);
    lua_pushboolean(L, true);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int get(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  unique_lock<mutex> l(g_vecTabLock);
  auto row = g_vecTab.find(name);
  if (row == g_vecTab.end()) {
    lua_pushstring(L, folly::stringPrintf("no such atomic vector: \"%s\"",
                                          name).c_str());
    luaL_error(L, "no such atomic vector: \"%s\"", name);
  }
  // Success; return userdata
  auto ptr = (TorchAtomicVectorIf**)
    lua_newuserdata(L, sizeof(TorchAtomicVectorIf*));
  *ptr = row->second.get();
  if (!luaT_pushmetatable(L, kTypeName)) {
    assert(false);
  }
  lua_setmetatable(L, -2);
  return 1;
}

int append(lua_State* L) {
  return checkAtomicVec(L, 1)->luaAppend(L);
}

int write(lua_State* L) {
  return checkAtomicVec(L, 1)->luaWrite(L);
}

int read(lua_State* L) {
  return checkAtomicVec(L, 1)->luaRead(L);
}

int size(lua_State* L) {
  return checkAtomicVec(L, 1)->luaSize(L);
}

const struct luaL_reg moduleFuncs[] = {
  { "create_float", createFloat },
  { "create_double", createDouble },
  { "destroy", destroy },
  { "get", get },
  { "append", append },

  { nullptr, nullptr },
};

const struct luaL_reg vecOps[] = {
  { "__index", read },
  { "__newindex", write },
  { "__len" , size },
};

} // namespace

extern "C" int LUAOPEN(lua_State* L) {
  // Vec ops.
  if (luaL_newmetatable(L, kTypeName)) {
    luaL_register(L, nullptr, vecOps);
    lua_pop(L, 1);
  }

  // Return module table.
  lua_newtable(L);
  luaL_register(L, nullptr, moduleFuncs);
  return 1;
}
