# FindBerkeleyDB_CXX.cmake - part of the DDGVec project
#
# Copyright (c) 2012, The Ohio State University
#
# This file is distributed under the terms described in LICENSE.TXT in the
# root directory.

# - Find BerkeleyDB
# Find the BerkeleyDB includes and library

find_package(PkgConfig)
pkg_check_modules(PC_DB QUIET DB)

find_path(DB_INCLUDE_DIR
          NAMES db_cxx.h
          HINTS ${PC_DB_INCLUDEDIR} ${PC_DB_INCLUDE_DIRS})

find_library(DB_LIBRARY
             NAMES db_cxx
             HINTS ${PC_DB_LIBDIR} ${PC_DB_LIBRARY_DIRS})

set(DB_LIBRARIES ${DB_LIBRARY})
set(DB_INCLUDE_DIRS ${DB_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DB DEFAULT_MSG DB_LIBRARY DB_INCLUDE_DIR) 
mark_as_advanced(DB_LIBRARY DB_INCLUDE_DIR)

