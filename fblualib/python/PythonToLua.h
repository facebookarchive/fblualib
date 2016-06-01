/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_PYTHONTOLUA_H_
#define FBLUA_PYTHON_PYTHONTOLUA_H_

#include <unordered_map>
#include "Utils.h"

namespace fblualib {
namespace python {

class PythonToLuaConverter {
 public:
  enum NoneMode {
    NONE_AS_LUA_NIL,
    NONE_AS_LUAPY_NONE,
  };
  explicit PythonToLuaConverter(NoneMode noneMode): noneMode_(noneMode) {}

  int convert(lua_State* L, const PyObjectHandle& obj);

 private:
  int doConvert(lua_State* L, const PyObjectHandle& obj);
  // We store all converted lua objects in a Lua list-like table at index
  // convertedIdx_. We can't keep them on the stack because the stack is
  // limited (and can't be raised above 8000 slots).
  int convertedIdx_ = 0;
  int convertedCount_ = 0;
  // Map from Python object into index in convertedIdx_
  std::unordered_map<PyObjectHandle, int> converted_;
  NoneMode noneMode_ = NONE_AS_LUA_NIL;
};

int initPythonToLua(lua_State* L);

}}  // namespaces

#endif /* FBLUA_PYTHON_PYTHONTOLUA_H_ */
