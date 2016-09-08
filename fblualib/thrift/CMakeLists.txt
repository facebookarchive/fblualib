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
FIND_PACKAGE(Thrift REQUIRED)
FIND_PACKAGE(Torch REQUIRED)
FIND_PACKAGE(THPP)
FIND_PACKAGE(FBLuaLib REQUIRED)

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIR}
  ${THRIFT_INCLUDE_DIR}
  ${THPP_INCLUDE_DIR}
  ${FBLUALIB_INCLUDE_DIR}
)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14")

SET(base_src
  ChunkedCompression.cpp
  Encoding.cpp
  LuaObject.cpp
)
ADD_THRIFT2(base_src "if/ChunkedCompression.thrift")
ADD_THRIFT2(base_src "if/LuaObject.thrift")

SET(base_h
  ChunkedCompression.h
  Encoding.h
  LuaObject.h
  LuaObject-inl.h
)

ADD_LIBRARY(fblualib_thrift SHARED ${base_src})
TARGET_LINK_LIBRARIES(fblualib_thrift
  ${FOLLY_LIBRARIES} ${GLOG_LIBRARIES} ${THRIFT_LIBRARIES}
  ${THPP_LIBRARIES})

SET(module_src
  Serialization.cpp
  LuaSerialization.cpp
)

ADD_LIBRARY(lua_module MODULE ${module_src})
SET_TARGET_PROPERTIES(lua_module PROPERTIES
  PREFIX ""
  OUTPUT_NAME "lib"
  COMPILE_DEFINITIONS "LUAOPEN=luaopen_fb_thrift_lib")
TARGET_LINK_LIBRARIES(lua_module
  fblualib_thrift luaT ${FBLUALIB_LIBRARIES})

SET(CMAKE_INSTALL_PREFIX ${SAVED_CMAKE_INSTALL_PREFIX})

INSTALL(TARGETS fblualib_thrift
        LIBRARY DESTINATION ${Torch_INSTALL_LIB})

INSTALL(FILES ${base_h}
        DESTINATION "${Torch_INSTALL_INCLUDE}/include/${MLI_INCLUDE_RELATIVE_OUTPUT_DIR}")
INSTALL_THRIFT2_HEADERS("if" "${Torch_INSTALL_INCLUDE}/include")

INSTALL(TARGETS lua_module
        DESTINATION "${ROCKS_LIBDIR}/fb/thrift")

INSTALL(DIRECTORY "fb"
        DESTINATION "${ROCKS_LUADIR}"
        FILES_MATCHING
        PATTERN "*.lua")
