/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "Utils.h"

namespace fblualib {
namespace python {

#ifndef NDEBUG
folly::ThreadLocal<int> PythonGuard::count_;
#endif

// Format the current Python exception as a string.
// Let Python do the work for us; call traceback.format_exception()
std::string formatPythonException() {
  if (!PyErr_Occurred()) {
    return "<no error?>";
  }

  // Retrieve Python exception and "normalize" (the docs are unclear but
  // they say you should do it :) )
  PyObject* exceptionObj;
  PyObject* valueObj;
  PyObject* tracebackObj;
  PyErr_Fetch(&exceptionObj, &valueObj, &tracebackObj);
  DCHECK(exceptionObj);
  PyErr_NormalizeException(&exceptionObj, &valueObj, &tracebackObj);

  PyObjectHandle exception(exceptionObj);
  PyObjectHandle value(valueObj);
  PyObjectHandle traceback(tracebackObj);

  // value and traceback may be null
  if (!value) {
    value.reset(PyObjectHandle::INCREF, Py_None);
  }
  if (!traceback) {
    traceback.reset(PyObjectHandle::INCREF, Py_None);
  }

  PyObjectHandle tbModule(PyImport_ImportModule("traceback"));
  if (!tbModule) {
    return "<import traceback failed>";
  }

  PyObject* tbDict = PyModule_GetDict(tbModule.get());  // borrowed
  if (!tbDict) {
    return "<no dict in traceback module>";
  }

  // borrowed
  PyObject* formatFunc = PyDict_GetItemString(tbDict, "format_exception");
  if (!formatFunc) {
    return "<no format_exception in traceback module>";
  }

  PyObjectHandle formatted(PyObject_CallFunction(
      formatFunc, const_cast<char*>("OOO"), exception.get(), value.get(),
      traceback.get()));
  if (!formatted) {
    return "<traceback.format_exception error>";
  }

  // format_exception returns a list of strings that should be concatenated.
  // Well then, let's do that.

  if (!PyList_Check(formatted.get())) {
    return "<traceback.format_exception didn't return a list>";
  }

  std::string out;
  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(formatted.get()); ++i) {
    PyObject* obj = PyList_GET_ITEM(formatted.get(), i);  // borrowed
    char* data;
    Py_ssize_t len;
    if (PyString_AsStringAndSize(obj, &data, &len) == -1) {
      return "<traceback.format_exception member not a string>";
    }
    out.append(data, len);
  }

  return out;
}

}}  // namespaces
