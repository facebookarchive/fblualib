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
#include <fblualib/CrossThreadRegistry.h>

#include <lua.hpp>
#include <fblualib/LuaUtils.h>
#include <memory>

using namespace fblualib;
using namespace fblualib::detail;
using namespace fblualib::thrift;
using namespace std;

// Teach AtomicVector how to refcount and serialize our tensors.
#define TENSOR_IMPL(T, Real)                          \
template<> struct Refcount<T*> {                      \
  void inc(T* t) {                                    \
    T ## _retain(t);                                  \
  }                                                   \
  void dec(T* t) {                                    \
    T ## _free(t);                                    \
  }                                                   \
};                                                    \
                                                      \
template<> struct Serde<T*> {                         \
  static folly::StringPiece save(T* t, StringWriter& sw) { \
    auto thpp = thpp::Tensor<Real>(t);                \
    auto luaObj = make(thpp);                         \
    const auto codec = thpp.size() > 1024 ?           \
      folly::io::CodecType::LZ4 :                     \
      folly::io::CodecType::NO_COMPRESSION;           \
    cppEncode(luaObj, codec, sw);                     \
    return folly::StringPiece(sw.finish());           \
  }                                                   \
  static T* load(folly::ByteRange* br) {              \
    StringReader sr(br);                              \
    auto decoded = cppDecode(sr);                     \
    auto thppTensor = getTensor<Real>(std::move(decoded)); \
    auto retval = thppTensor.moveAsTH();              \
    /* Caller's job to incref if necessary. */        \
    return retval;                                    \
  }                                                   \
}

TENSOR_IMPL(THFloatTensor, float);
TENSOR_IMPL(THDoubleTensor, double);
TENSOR_IMPL(THIntTensor, int);

#undef TENSOR_IMPL

namespace {

constexpr const char* kTypeName = "fblualib.atomicvector";

struct TorchAtomicVectorIf {
  typedef int BucketIndex;
  virtual ~TorchAtomicVectorIf() { }

  virtual int luaRead(lua_State* L) = 0;
  virtual int luaWrite(lua_State* L) = 0;
  virtual int luaAppend(lua_State* L) = 0;
  virtual int luaSize(lua_State* L) = 0;
  virtual int luaSave(lua_State* L) = 0;
  virtual int luaLoad(lua_State* L) = 0;
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
    size_t sz = m_av.append(checkTensor(L, 2));
    lua_pushnumber(L, sz + 1); // To lua
    return 1;
  }

  virtual int luaSize(lua_State* L) {
    lua_pushnumber(L, m_av.size());
    return 1;
  }

  virtual int luaLoad(lua_State* L) {
    auto file = luaDecodeFILE(L, 2);
    m_av.load(file);
    return 0;
  }

  virtual int luaSave(lua_State* L) {
    auto file = luaDecodeFILE(L, 2);
    m_av.save(file);
    return 0;
  }
};

CrossThreadRegistry<string, TorchAtomicVectorIf> g_vecTab;

template<typename Real>
int create(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  auto created = g_vecTab.create(name, [] {
    return folly::make_unique<TorchAtomicVector<Real>>();
  });
  if (created) {
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

int createInt(lua_State* L) {
  return create<int>(L);
}

int destroy(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  auto removed = g_vecTab.erase(name);
  if (removed) {
    lua_pushboolean(L, true);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int get(lua_State* L) {
  auto name = luaL_checkstring(L, 1);
  auto ptr = g_vecTab.get(name);
  if (!ptr) {
    lua_pushstring(L, folly::stringPrintf("no such atomic vector: \"%s\"",
                                          name).c_str());
    luaL_error(L, "no such atomic vector: \"%s\"", name);
  }
  // Success; return userdata
  auto luaPtr = (TorchAtomicVectorIf**)
    lua_newuserdata(L, sizeof(TorchAtomicVectorIf*));
  *luaPtr = ptr;
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

int load(lua_State* L) {
  return checkAtomicVec(L, 1)->luaLoad(L);
}

int save(lua_State* L) {
  return checkAtomicVec(L, 1)->luaSave(L);
}

const struct luaL_reg moduleFuncs[] = {
  { "create_float", createFloat },
  { "create_double", createDouble },
  { "create_int", createInt },
  { "destroy", destroy },
  { "get", get },
  { "append", append },

  { "load", load },
  { "save",  save },

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
