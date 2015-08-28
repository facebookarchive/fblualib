#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#
# - Try to find fblualib
# This will define
# FBLUALIB_FOUND
# FBLUALIB_INCLUDE_DIR
# FBLUALIB_LIBRARIES

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)

FIND_LIBRARY(FBLUALIB_LIBRARY fblualib  HINTS ${Torch_INSTALL_LIB})
FIND_PATH(FBLUALIB_INCLUDE_DIR "fblualib/LuaUtils.h"  HINTS ${Torch_INSTALL_INCLUDE})

SET(FBLUALIB_LIBRARIES ${FBLUALIB_LIBRARY})

FIND_PACKAGE_HANDLE_STANDARD_ARGS(Folly
  REQUIRED_ARGS FBLUALIB_INCLUDE_DIR FBLUALIB_LIBRARIES)
