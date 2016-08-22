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
FIND_PACKAGE(Torch REQUIRED)
FIND_PACKAGE(FBLuaLib REQUIRED)

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
)

INCLUDE_DIRECTORIES(${LUA_INCDIR})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

SET(src
  Util.cpp
  Reactor.cpp
)

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

ADD_LIBRARY(fbutil MODULE ${src})
TARGET_LINK_LIBRARIES(fbutil ${FOLLY_LIBRARIES})
TARGET_LINK_LIBRARIES(fbutil ${FBLUALIB_LIBRARIES})

install(TARGETS fbutil LIBRARY
  DESTINATION ${ROCKS_LIBDIR})

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
