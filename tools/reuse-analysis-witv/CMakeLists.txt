include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS} -fPIC)

llvm_map_components_to_libraries(LLVM_LIBRARIES bitreader asmparser analysis)


INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/include")

#
# Define our sources.
#
SET(_SOURCES  "src/Main.cxx")
              
SET(_HEADERS  "include/ConvexPartitioning.hxx"
				"include/MacroDAG.hxx")



ADD_EXECUTABLE(reuse-analysis-witv-v2
               ${_SOURCES}
               ${_HEADERS})
               
target_link_libraries(reuse-analysis-witv-v2
  ddg-analysis
  ${LLVM_LIBRARIES}
  ${Boost_LIBRARIES}
)

install(TARGETS reuse-analysis-witv-v2 DESTINATION bin)

