include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS} -fPIC)

llvm_map_components_to_libraries(LLVM_LIBRARIES bitreader asmparser analysis)

add_executable(dynamic-graph dynamic-graph.cxx)
target_link_libraries(dynamic-graph
  ddg-analysis
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
)

add_executable(value-flow-graph value-flow-graph.cxx)
target_link_libraries(value-flow-graph
  ddg-analysis
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
)

install(TARGETS dynamic-graph DESTINATION bin)
install(TARGETS value-flow-graph DESTINATION bin)

