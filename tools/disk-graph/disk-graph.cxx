#include <llvm/LLVMContext.h>
#include <llvm/Constants.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>

#include "ddg/analysis/DiskCDAG.hxx"

#include <queue>

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

  string programNameWithExt = InputFilename.substr(InputFilename.find_last_of("\\/")+1, InputFilename.length());
  const string programName = programNameWithExt.substr(0, programNameWithExt.find("."));

  clock_t begin = clock();
  DiskCDAG<GraphNode> *cdag = DiskCDAG<GraphNode>::generateGraph<GraphNode, DiskCDAGBuilder<GraphNode> >(ids, programName); 
  clock_t end = clock();
  double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
  cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;

  if(cdag)
  {
    cout << "\n";

    cout << "\n";
    begin = clock();
    cdag->performTraversal("bfsOut", false);
    end = clock();
    elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
    cout << "\n Time taken for BFS traversal (in mins) : " << elapsed_time / 60;

    cout << "\n";
    begin = clock();
    cdag->performTraversal("dfsOut", true);
    end = clock();
    elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
    cout << "\n Time taken for DFS traversal (in mins) : " << elapsed_time / 60;

    cout << "\n";
    begin = clock();
    cdag->performTopoSort("toposortwithq", false);
    end = clock();
    elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
    cout << "\n Time taken for Topological Sort using queue (in mins) : " << elapsed_time / 60;

    cout << "\n";

    cout << "\n";
    begin = clock();
    cdag->performTopoSort("toposortwithstack", true);
    end = clock();
    elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
    cout << "\n Time taken for Topological Sort using stack (in mins) : " << elapsed_time / 60;

    cout << "\n";

    delete cdag;
  }
  else
  {
    std::cerr << "Fatal : Failed to generate graph \n";
  }
  cout << '\n';
}