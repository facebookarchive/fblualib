/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_REF_H_
#define FBLUA_PYTHON_REF_H_

#include "Utils.h"

namespace fblualib {
namespace python {

// Opaque Python reference
struct OpaqueRef {
  explicit OpaqueRef(PyObjectHandle o) : obj(std::move(o)) {
    if (obj) {
      debugAddPythonRef(obj.get());
    }
  }
  ~OpaqueRef() {
    if (obj) {
      debugDeletePythonRef(obj.get());
    }
  }
  PyObjectHandle obj;
};

OpaqueRef* checkOpaqueRef(lua_State* L, int index);
OpaqueRef* getOpaqueRef(lua_State* L, int index);
int pushOpaqueRef(lua_State* L, PyObjectHandle obj);

int initRef(lua_State* L);

}}  // namespaces

#endif /* FBLUA_PYTHON_REF_H_ */
