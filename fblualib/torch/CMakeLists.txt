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

FIND_PACKAGE(Torch REQUIRED)

INCLUDE(MultiLevelIncludes)
MLI_SET_DEPTH(2)

FIND_PACKAGE(Folly REQUIRED)
FIND_PACKAGE(Torch REQUIRED)

INCLUDE_DIRECTORIES(
  ${FOLLY_INCLUDE_DIR}
)

INCLUDE_DIRECTORIES(${LUA_INCDIR})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

SET(src
  AsyncRNG.cpp
)

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

ADD_LIBRARY(async_rng MODULE ${src})
set_target_properties(async_rng
                      PROPERTIES PREFIX "")

TARGET_LINK_LIBRARIES(async_rng ${FOLLY_LIBRARIES} fblualib)

install(TARGETS async_rng LIBRARY
  DESTINATION "${ROCKS_LIBDIR}/fb/torch")
