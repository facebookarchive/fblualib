#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#
# THRIFT_FOUND
# THRIFT_INCLUDE_DIR
# THRIFT_LIBRARIES
#
# ADD_THRIFT2 and INSTALL_THRIFT2_HEADERS assume that you are using
# MultiLevelIncludes.

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)
INCLUDE(MultiLevelIncludes)

FIND_LIBRARY(THRIFT_LIBRARY thrift)
FIND_LIBRARY(THRIFT_CPP2_LIBRARY thriftcpp2)
FIND_PATH(THRIFT_INCLUDE_DIR "thrift/lib/cpp2/Thrift.h")

SET(THRIFT_LIBRARIES ${THRIFT_LIBRARY} ${THRIFT_CPP2_LIBRARY})

# Add a Thrift2 file.
# Add the source files to src_var in parent scope.
# Does not support services (yet).
FUNCTION(ADD_THRIFT2 src_var fn)
    GET_FILENAME_COMPONENT(dir ${fn} PATH)
    # NAME_WE = name without extension. Because this is the one place where
    # cmake developers have decided that verbosity is bad.
    GET_FILENAME_COMPONENT(bnwe ${fn} NAME_WE)
    SET(absroot "${MLI_INCLUDE_OUTPUT_DIR}/${dir}")
    SET(abspath "${absroot}/gen-cpp2")
    FILE(MAKE_DIRECTORY "${absroot}")
    SET(h_files "${abspath}/${bnwe}_types.h"
                "${abspath}/${bnwe}_types.tcc"
                "${abspath}/${bnwe}_constants.h")
    SET(src_files "${abspath}/${bnwe}_types.cpp"
                  "${abspath}/${bnwe}_constants.cpp")
    GET_DIRECTORY_PROPERTY(includes INCLUDE_DIRECTORIES)
    FOREACH(d ${includes})
      SET(include_args ${include_args} "-I" ${d})
    ENDFOREACH()
    ADD_CUSTOM_COMMAND(
      OUTPUT ${h_files} ${src_files}
      COMMAND python ARGS
        -mthrift_compiler.main
        --gen cpp2:include_prefix
        ${include_args}
        "${CMAKE_CURRENT_SOURCE_DIR}/${fn}"

      DEPENDS "${fn}"
      WORKING_DIRECTORY "${absroot}")

    SET(${src_var} ${${src_var}} ${src_files} PARENT_SCOPE)
ENDFUNCTION()

# Install all Thrift2 headers from a directory
# Does not support services (yet).
FUNCTION(INSTALL_THRIFT2_HEADERS dir dest)
    SET(relpath "${dir}/gen-cpp2")
    SET(abspath "${MLI_INCLUDE_OUTPUT_DIR}/${relpath}")
    INSTALL(DIRECTORY "${abspath}/"
            DESTINATION "${dest}/${MLI_INCLUDE_RELATIVE_OUTPUT_DIR}/${relpath}"
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.tcc")

    SET(relpath "${dir}")
    SET(abspath "${CMAKE_CURRENT_SOURCE_DIR}/${relpath}")
    INSTALL(DIRECTORY "${abspath}/"
            DESTINATION "${dest}/${MLI_INCLUDE_RELATIVE_OUTPUT_DIR}/${relpath}"
            FILES_MATCHING
            PATTERN "*.thrift")
ENDFUNCTION()

FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  Thrift
  REQUIRED_ARGS
    THRIFT_INCLUDE_DIR
    THRIFT_LIBRARY
    THRIFT_CPP2_LIBRARY)
