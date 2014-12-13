# CMakeLists.txt - part of the DDGVec project
#
# Copyright (c) 2012, The Ohio State University
#
# This file is distributed under the terms described in LICENSE.TXT in the
# root directory.

include_directories(${LLVM_INCLUDE_DIRS})

add_definitions(${LLVM_DEFINITIONS})
add_definitions(-fno-rtti)

add_llvm_loadable_module(ddg-instr
  InstrumentDDG.cxx
  Visitors.cxx
)

install(TARGETS ddg-instr LIBRARY DESTINATION lib)

