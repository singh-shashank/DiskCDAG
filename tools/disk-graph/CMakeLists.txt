include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS} -fPIC)

llvm_map_components_to_libraries(LLVM_LIBRARIES bitreader asmparser analysis)

add_executable(disk-graph disk-graph.cxx)
target_link_libraries(disk-graph
  ddg-analysis
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
)

install(TARGETS disk-graph DESTINATION bin)

