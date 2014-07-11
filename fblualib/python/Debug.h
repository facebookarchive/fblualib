/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_DEBUG_H_
#define FBLUA_PYTHON_DEBUG_H_

#include <Python.h>

namespace fblualib {
namespace python {

#ifndef NDEBUG

void debugAddLuaRef(const void* obj);
void debugDeleteLuaRef(const void* obj);
void debugCheckLuaRef(const void* obj);
void debugCheckNoLuaRefs();

void debugAddPythonRef(const PyObject* obj);
void debugDeletePythonRef(const PyObject* obj);
void debugCheckPythonRef(const PyObject* obj);
void debugCheckNoPythonRefs();

void debugSetWatermark();
void debugCheckNoRefs();

#else  /* NDEBUG */

inline void debugAddLuaRef(const void* obj) { }
inline void debugDeleteLuaRef(const void* obj) { }
inline void debugCheckLuaRef(const void* obj) { }
inline void debugCheckNoLuaRefs() { }

inline void debugAddPythonRef(const PyObject* obj) { }
inline void debugDeletePythonRef(const PyObject* obj) { }
inline void debugCheckPythonRef(const PyObject* obj) { }
inline void debugCheckNoPythonRefs() { }

inline void debugSetWatermark() { }
inline void debugCheckNoRefs() { }

#endif

}}  // namespaces

#endif /* FBLUA_PYTHON_DEBUG_H_ */
