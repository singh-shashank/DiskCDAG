# CMakeLists.txt - part of the DDGVec project
#
# Copyright (c) 2012, The Ohio State University
#
# This file is distributed under the terms described in LICENSE.TXT in the
# root directory.

cmake_minimum_required(VERSION 2.6)
project(ddg-analysis)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Boost COMPONENTS iostreams REQUIRED program_options REQUIRED)
find_package(LLVM REQUIRED)

include(AddLLVM)

include_directories(include)
include_directories(${Boost_INCLUDE_DIRS})

add_subdirectory(lib/instrument)
add_subdirectory(lib/analysis)
add_subdirectory(lib/runtime)
add_subdirectory(tools/reuse-analysis)
add_subdirectory(tools/reuse-analysis-witv)
add_subdirectory(tools/disk-graph)
add_subdirectory(tools/ddg-vect)
#add_subdirectory(tools/dynamic-graph)
#add_subdirectory(tools/ie-analysis)
#add_subdirectory(tools/find-reductions)
#add_subdirectory(tools/ddg-parallel)
add_subdirectory(tools/count-loops)
add_subdirectory(tools/count-nodes)
#add_subdirectory(tools/parallel-prof)
#add_subdirectory(tools/loop-prof)
#add_subdirectory(tools/print-ddg)

