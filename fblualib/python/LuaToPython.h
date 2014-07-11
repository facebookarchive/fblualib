/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_LUATOPYTHON_H_
#define FBLUA_PYTHON_LUATOPYTHON_H_

#include <unordered_map>
#include "Utils.h"

namespace fblualib {
namespace python {

// Helper class to convert Lua objects to Python objects
class LuaToPythonConverter {
 public:
  enum : unsigned {
    IGNORE_INVALID_TYPES = 1U << 0,
    BUILTIN_TYPES_ONLY = 1U << 1,
    INTEGRAL_NUMBERS = 1U << 2,
  };

  PyObjectHandle convert(lua_State* L, int index, unsigned flags = 0);

  PyObjectHandle convertDefault(lua_State* L, int index) {
    return convert(L, index, 0);
  }
  PyObjectHandle convertToFloat(lua_State* L, int index);
  PyObjectHandle convertToInt(lua_State* L, int index);
  PyObjectHandle convertToLong(lua_State* L, int index);
  PyObjectHandle convertToBytes(lua_State* L, int index);
  PyObjectHandle convertToUnicode(lua_State* L, int index);
  PyObjectHandle convertToList(lua_State* L, int index);
  PyObjectHandle convertToTuple(lua_State* L, int index);
  PyObjectHandle convertToFastSequence(lua_State* L, int index);
  PyObjectHandle convertToDict(lua_State* L, int index);

 private:
  PyObjectHandle checkRecorded(lua_State* L, int index);
  void record(lua_State* L, int index, const PyObjectHandle& obj);
  PyObjectHandle convertFromTable(lua_State* L, int index);
  PyObjectHandle convertListFromTable(lua_State* L, int index, bool rec=true);
  PyObjectHandle convertTupleFromTable(lua_State* L, int index, bool rec=true);
  PyObjectHandle convertDictFromTable(lua_State* L, int index, bool rec=true);

  template <class T>
  PyObjectHandle convertTensor(lua_State* L, thpp::Tensor<T>& tensor,
                               int numpyType);

  std::unordered_map<const void*, PyObjectHandle> converted_;
};

int initLuaToPython(lua_State* L);

}}  // namespaces

#endif /* FBLUA_PYTHON_LUATOPYTHON_H_ */
