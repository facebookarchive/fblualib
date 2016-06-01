/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef FBLUA_PYTHON_UTILS_H_
#error This file may only be included from Utils.h
#endif

#include <boost/format.hpp>

namespace fblualib {
namespace python {

namespace detail {

inline std::string format() { return ""; }

inline boost::format& format_helper(boost::format& msg) { return msg; }

template <typename H, typename... Tail>
boost::format& format_helper(boost::format& msg, H&& val, Tail&&... args) {
  return format_helper(msg % std::forward<H>(val), std::forward<Tail>(args)...);
}

template <typename... Args>
std::string format(std::string fmt, Args&&... args) {
  boost::format msg(fmt);
  return format_helper(msg, std::forward<Args>(args)...).str();
}
}  // namespace detail

// Raise the current Python error as a Lua error
template <class... Args>
void raisePythonError(lua_State* L, Args&&... args) {
  luaL_error(L, "Python error: %s\n%s",
             detail::format(std::forward<Args>(args)...).c_str(),
             formatPythonException().c_str());
}

// Check a condition; if false, raise the Python error as a lua error.
template <class Cond, class... Args>
inline void checkPythonError(Cond&& cond, lua_State* L, Args&&... args) {
  if (!cond) {
    raisePythonError(L, std::forward<Args>(args)...);
  }
}

#ifdef __GNUC__
#define X_UNUSED __attribute__((__unused__))
#else
#define X_UNUSED
#endif

// Yes, static.
// The numpy C API is implemented using macros; PyArray_Foo is expanded
// to (*PyArray_API->foo), and PyArray_API is declared as static in a header
// file. This means it must be initialized (by calling import_array) in
// *every* file where it is used.
namespace detail {

// The numpy C API is implemented using macros; PyArray_Foo is expanded
// to (*PyArray_API->foo), and PyArray_API is declared as static in a header
// file. This means it must be initialized (by calling import_array) in
// *every* file where it is used.

X_UNUSED static bool doImportArray() {
  // import_array* are macros that return (!!!)
  import_array1(false);
  return true;
}

}  // namespace detail

X_UNUSED static int initNumpy(lua_State* L) {
  PythonGuard g;
  checkPythonError(detail::doImportArray(), L, "import numpy array module");
  return 0;
}

#undef X_UNUSED

inline void fixIndex(lua_State* L, int& index) {
  if (index < 0) {
    index = lua_gettop(L) + index + 1;
  }
}

}}  // namespaces
