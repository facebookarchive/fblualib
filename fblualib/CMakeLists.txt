#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

# Torch messes this up
SET(SAVED_CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

# Use our own cmake scripts
SET(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_MODULE_PATH}")

INCLUDE(MultiLevelIncludes)
MLI_SET_DEPTH(1)

FIND_PACKAGE(Folly REQUIRED)
FIND_PACKAGE(Glog REQUIRED)
FIND_PACKAGE(Torch REQUIRED)
FIND_PACKAGE(THPP REQUIRED)

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
  ${GLOG_INCLUDE_DIR}
  ${THPP_INCLUDE_DIR}
)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14")

SET(src
  LuaUtils.cpp
  Future.cpp
  Reactor.cpp
  UserData.cpp
)

SET(h
  LuaUtils.h
  LuaUtils-inl.h
  Future.h
  Reactor.h
  UserData.h
  CrossThreadRegistry.h
)

SET(CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-as-needed")
ADD_LIBRARY(fblualib SHARED ${src})

FIND_LIBRARY(lib_luaT luaT
  HINTS ${Torch_INSTALL_LIB})
IF(NOT lib_luaT)
  MESSAGE(FATAL_ERROR "libluaT not found")
ENDIF()
TARGET_LINK_LIBRARIES(fblualib
  ${lib_luaT} ${THPP_LIBRARIES} ${FOLLY_LIBRARIES} ${GLOG_LIBRARIES})

INSTALL(TARGETS fblualib
        LIBRARY DESTINATION ${Torch_INSTALL_LIB})

INSTALL(FILES ${h}
        DESTINATION "${Torch_INSTALL_INCLUDE}/${MLI_INCLUDE_RELATIVE_OUTPUT_DIR}")
