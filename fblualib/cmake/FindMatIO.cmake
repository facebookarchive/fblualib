#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#
# This will define
# MATIO_FOUND
# MATIO_INCLUDE_DIR
# MATIO_LIBRARIES

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)

FIND_LIBRARY(MATIO_LIBRARY matio)
FIND_PATH(MATIO_INCLUDE_DIR "matio.h")

SET(MATIO_LIBRARIES ${MATIO_LIBRARY})

FIND_PACKAGE_HANDLE_STANDARD_ARGS(Folly
  REQUIRED_ARGS MATIO_INCLUDE_DIR MATIO_LIBRARIES)
