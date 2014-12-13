include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS} -fPIC)

add_library(ddg-analysis Ids.cxx Trace.cxx DynamicGraph.cxx
		DynInstCounter.cxx)

