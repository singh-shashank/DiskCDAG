# CMakeLists.txt - part of the DDGVec project
#
# Copyright (c) 2012, The Ohio State University
#
# This file is distributed under the terms described in LICENSE.TXT in the
# root directory.

add_library(ddg-rt SHARED Runtime.cxx)
target_link_libraries(ddg-rt ${Boost_LIBRARIES})

add_library(ddg-count-traces SHARED TraceCount.cxx)

install(TARGETS ddg-rt ddg-count-traces LIBRARY DESTINATION lib)

