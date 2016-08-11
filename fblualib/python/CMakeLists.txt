#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

ENABLE_TESTING()

# Torch messes this up
SET(SAVED_CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

SET(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_MODULE_PATH}")

ADD_DEFINITIONS(-DNO_FOLLY)
ADD_DEFINITIONS(-DNO_THRIFT)
ADD_DEFINITIONS(-DNDEBUG)

INCLUDE(MultiLevelIncludes)
MLI_SET_DEPTH(1)

FIND_PACKAGE(Glog REQUIRED)
FIND_PACKAGE(Torch REQUIRED)

SET(CMAKE_PREFIX_PATH ${Torch_INSTALL_PREFIX})
FIND_PACKAGE(THPP REQUIRED)
FIND_PACKAGE(Boost REQUIRED COMPONENTS thread)

FIND_PACKAGE(PythonInterp 2.7)
if(APPLE)
  find_program(PYTHON_CONFIG_EXECUTABLE python-config)
  if (NOT PYTHON_CONFIG_EXECUTABLE)
    message(SEND_ERROR "python-config executable not found, but python is required.")
  endif()
  # using "python-config --prefix" so that cmake always uses the python that is
  # in the user's path, this is a fix for homebrew on Mac:
  # https://cmake.org/Bug/view.php?id=14809
  execute_process(COMMAND ${PYTHON_CONFIG_EXECUTABLE} --prefix OUTPUT_VARIABLE python_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(PYTHON_INCLUDE_DIR ${python_prefix}/include/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR})
  set(PYTHON_LIBRARY ${python_prefix}/lib/libpython${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()
FIND_PACKAGE(PythonLibs ${PYTHON_VERSION_STRING} REQUIRED)
IF (PYTHONLIBS_VERSION_STRING VERSION_GREATER "3" AND PYTHONLIBS_VERSION_STRING VERSION_GREATER "2")
  MESSAGE(FATAL_ERROR "Python Version should be < 3.0, but found python version: ${PYTHONLIBS_VERSION_STRING}")
ENDIF (PYTHONLIBS_VERSION_STRING VERSION_GREATER "3" AND PYTHONLIBS_VERSION_STRING VERSION_GREATER "2")

FIND_PACKAGE(NumPy REQUIRED)
FIND_LIBRARY(lib_luaT luaT HINTS ${Torch_INSTALL_LIB})
IF(NOT lib_luaT)
  MESSAGE(FATAL_ERROR "libluaT not found")
ENDIF()

INCLUDE_DIRECTORIES(
  ${GLOG_INCLUDE_DIR}
  ${THPP_INCLUDE_DIR}
  ${PYTHON_INCLUDE_DIRS}
  ${NUMPY_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

SET(src
  Debug.cpp
  Lib.cpp
  LuaToPython.cpp
  NumpyArrayAllocator.cpp
  PythonToLua.cpp
  Ref.cpp
  Storage.cpp
  Utils.cpp
)

ADD_LIBRARY(lua_module MODULE ${src})
SET_TARGET_PROPERTIES(lua_module PROPERTIES
  PREFIX ""
  OUTPUT_NAME "lib"
  COMPILE_DEFINITIONS "LUAOPEN=luaopen_fb_python_lib")
TARGET_LINK_LIBRARIES(lua_module
  dl
  luajit
  TH
  ${lib_luaT}
  ${Boost_THREAD_LIBRARIES}
  ${GLOG_LIBRARIES}
  ${THPP_LIBRARIES}
  ${PYTHON_LIBRARIES}
)

SET(CMAKE_INSTALL_PREFIX ${SAVED_CMAKE_INSTALL_PREFIX})

INSTALL(TARGETS lua_module
        DESTINATION "${ROCKS_LIBDIR}/fb/python")

INSTALL(DIRECTORY "fb"
        DESTINATION "${ROCKS_LUADIR}"
        FILES_MATCHING
        PATTERN "*.lua")
