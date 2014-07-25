#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

ENABLE_TESTING()

SET(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/../cmake"
  "${CMAKE_MODULE_PATH}")

INCLUDE(MultiLevelIncludes)
MLI_SET_DEPTH(2)

FIND_PACKAGE(Folly REQUIRED)

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

SET(src
  FFIVector.cpp
)

# I wish I could install the FFI module with LuaRocks's standard installation
# methods, but LuaRocks can only install shared libraries in package.cpath,
# and you don't even get to know what the final location is (because files
# are first installed in a staging directory and then copied from there)
SET(lib_dir "${CMAKE_INSTALL_PREFIX}/lib/fblualib/ffivector")

ADD_LIBRARY(cpp MODULE ${src})
TARGET_LINK_LIBRARIES(cpp ${FOLLY_LIBRARIES})
SET(clib
  "${lib_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}cpp${CMAKE_SHARED_LIBRARY_SUFFIX}")

CONFIGURE_FILE(
  "fb/ffivector/_config.lua.in"
  "fb/ffivector/_config.lua"
  @ONLY)

INSTALL(TARGETS cpp
        LIBRARY DESTINATION "${lib_dir}")

INSTALL(
  DIRECTORY "fb"
  DESTINATION "${ROCKS_LUADIR}"
  FILES_MATCHING
  PATTERN "*.lua")

INSTALL(
  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fb"
  DESTINATION "${ROCKS_LUADIR}"
  FILES_MATCHING
  PATTERN "*.lua")
