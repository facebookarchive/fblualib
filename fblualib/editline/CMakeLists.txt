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

FIND_LIBRARY(libedit NAMES edit libedit libedit.so.2)
IF(NOT libedit)
  MESSAGE(FATAL_ERROR "libedit required")
ENDIF(NOT libedit)

CONFIGURE_FILE(
  "fb/editline/_config.lua.in"
  "fb/editline/_config.lua"
  @ONLY)

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
