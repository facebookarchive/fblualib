/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_STORAGE_H_
#error This file may only be included from Storage.h
#endif

namespace fblualib {
namespace python {

template <class T>
void PythonStorage<T>::define() {
  pythonType.ob_type = &PyType_Type;
  PyType_Ready(&pythonType);
}

template <class T>
Py_ssize_t PythonStorage<T>::getBuffer(PyObject* selfObj,
                                       Py_ssize_t segment,
                                       void** ptrptr) {
  if (segment != 0) {
    PyErr_SetString(PyExc_ValueError, "Invalid segment");
    return -1;
  }

  *ptrptr = self(selfObj)->storage_.data();
  return self(selfObj)->storage_.size() * sizeof(T);
}

template <class T>
Py_ssize_t PythonStorage<T>::segCount(PyObject* selfObj, Py_ssize_t* lenp) {
  if (lenp) {
    *lenp = self(selfObj)->storage_.size() * sizeof(T);
  }
  return 1;
}

template <class T>
void PythonStorage<T>::deallocate(PyObject* selfObj) {
  // Call destructor directly, then deallocate memory the Python way.
  self(selfObj)->~PythonStorage();
  debugDeletePythonRef(selfObj);
  (*pythonType.tp_free)(selfObj);
}

template <class T>
PyBufferProcs PythonStorage<T>::pythonBufferProcs = {
  &getBuffer,               /*bf_getreadbuffer*/
  &getBuffer,               /*bf_getwritebuffer*/
  &segCount,                /*bf_getsegcount*/
  nullptr,                  /*bf_getcharbuffer*/
};

template <class T>
PyTypeObject PythonStorage<T>::pythonType = {
  PyObject_HEAD_INIT(nullptr)
  0,                         /*ob_size*/
  "torch.Storage", /* XXX */ /*tp_name*/
  sizeof(PythonStorage),     /*tp_basicsize*/
  0,                         /*tp_itemsize*/
  deallocate,                /*tp_dealloc*/
  0,                         /*tp_print*/
  0,                         /*tp_getattr*/
  0,                         /*tp_setattr*/
  0,                         /*tp_compare*/
  0,                         /*tp_repr*/
  0,                         /*tp_as_number*/
  0,                         /*tp_as_sequence*/
  0,                         /*tp_as_mapping*/
  0,                         /*tp_hash */
  0,                         /*tp_call*/
  0,                         /*tp_str*/
  0,                         /*tp_getattro*/
  0,                         /*tp_setattro*/
  &pythonBufferProcs,        /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GETCHARBUFFER,  /*tp_flags*/
  "torch Storage objects",   /*tp_doc*/
};

}}  // namespaces
