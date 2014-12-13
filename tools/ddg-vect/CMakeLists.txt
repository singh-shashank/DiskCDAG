# CMakeLists.txt - part of the DDGVec project
#
# Copyright (c) 2012, The Ohio State University
#
# This file is distributed under the terms described in LICENSE.TXT in the
# root directory.

find_package(BerkeleyDB_CXX REQUIRED)

include_directories(
  ${LLVM_INCLUDE_DIRS}
  ${DB_INCLUDE_DIR}
)
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS} -fPIC)
llvm_map_components_to_libraries(LLVM_LIBRARIES bitreader asmparser analysis)

add_library(partition-graph LazyPartitionGraph.cxx TimestampVector.cxx)
target_link_libraries(partition-graph ddg-analysis)

add_executable(ddg-vect ddg-vect.cxx)
target_link_libraries(ddg-vect
  partition-graph
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
  ${DB_LIBRARIES}
)

add_library(ddg-vect-rt SHARED ddg-vect-rt.cxx)
target_link_libraries(ddg-vect-rt
  partition-graph
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
  ${DB_LIBRARIES}
)

install(TARGETS ddg-vect DESTINATION bin)
install(TARGETS ddg-vect-rt DESTINATION lib)

