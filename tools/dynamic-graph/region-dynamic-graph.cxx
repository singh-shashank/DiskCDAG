#include "ddg/analysis/DynInstCounter.hxx"
#include "ddg/analysis/DynamicGraph.hxx"

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>


using namespace ddg;
using namespace llvm;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::Required, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));


int main(int argc, char **argv)
{

  cl::ParseCommandLineOptions(argc, argv, "DDG Analysis tool\n");

  llvm_shutdown_obj shutdownObj;  // Call llvm_shutdown() on exit.
  LLVMContext &context = getGlobalContext();

  SMDiagnostic err;

  auto_ptr<Module> module; 
  module.reset(ParseIRFile(InputFilename, err, context));

  if (module.get() == 0) {
    err.print(argv[0], errs());
    return 1;
  }

  Ids ids;
  ids.runOnModule(*module.get());

  DynInstCounter counter(ids);
  int count = counter.getCount();
  std::cout << " Instruction count = " << count << '\n';
  DynInstCounter counter2(ids);
  int regionCount = counter2.getRegionCount();
  std::cout << "region count  = " << regionCount << '\n';
  DynamicGraph *dynGraph = DynamicGraph::generateGraph(ids, count);

  //dynGraph->printGraph();
  //dynGraph->printDOTGraph("dyn_graph.dot");
  //std::cout << "count: " << count << "\n";
  //std::cout << "numnodes: " << dynGraph->getNumNodes() << "\n";

  delete dynGraph;
}

