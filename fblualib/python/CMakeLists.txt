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
  "${CMAKE_CURRENT_SOURCE_DIR}/../cmake"
  "${CMAKE_MODULE_PATH}")

INCLUDE(MultiLevelIncludes)
MLI_SET_DEPTH(2)

FIND_PACKAGE(Folly REQUIRED)
FIND_PACKAGE(Glog REQUIRED)
FIND_PACKAGE(Torch REQUIRED)
FIND_PACKAGE(THPP REQUIRED)
FIND_PACKAGE(FBLuaLib REQUIRED)
FIND_PACKAGE(PythonLibs REQUIRED)
FIND_PACKAGE(NumPy REQUIRED)

if (${PYTHONLIBS_VERSION_STRING} GREATER "3" )
    message(STATUS "Found Python version > 3, linking against 2.7")
    string(REGEX REPLACE "3\\.[0-9][a-z]*" "2.7" PYTHON_LIBRARIES ${PYTHON_LIBRARIES})
    string(REGEX REPLACE "3\\.[0-9][a-z]*" "2.7" PYTHON_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})
endif (${PYTHONLIBS_VERSION_STRING} GREATER "3" )

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIR}
  ${THPP_INCLUDE_DIR}
  ${FBLUALIB_INCLUDE_DIR}
  ${PYTHON_INCLUDE_DIRS}
  ${NUMPY_INCLUDE_DIRS}
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
  dl ${FOLLY_LIBRARIES} ${GLOG_LIBRARIES} ${THPP_LIBRARIES}
  ${FBLUALIB_LIBRARIES} ${PYTHON_LIBRARIES})

SET(CMAKE_INSTALL_PREFIX ${SAVED_CMAKE_INSTALL_PREFIX})

INSTALL(TARGETS lua_module
        DESTINATION "${ROCKS_LIBDIR}/fb/python")

INSTALL(DIRECTORY "fb"
        DESTINATION "${ROCKS_LUADIR}"
        FILES_MATCHING
        PATTERN "*.lua")
