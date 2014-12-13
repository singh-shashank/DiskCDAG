#include "ddg/analysis/DynamicGraph.hxx"
#include "ddg/analysis/DynInstCounter.hxx"

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
  DynamicGraph *dynGraph = DynamicGraph::generateGraph(ids, count);
  DynamicGraph *vfg = dynGraph->extractValueFlowGraph();

  vfg->printGraph();
  vfg->printDOTGraph("vfg.dot");

  delete vfg;
  delete dynGraph;
}

