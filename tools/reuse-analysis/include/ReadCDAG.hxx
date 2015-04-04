#include "GraphAnalysis.hxx"
#include "GraphNode.hxx"

#include "ddg/analysis/DiskCDAG.hxx"
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>


#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <ctime>


using namespace std;
using namespace GraphAnalysis;

using namespace ddg;
using namespace llvm;

class ReadCDAG{


public:
  ReadCDAG(){}

  ~ReadCDAG(){
    list<GraphNode*>::iterator it = graphNodeCDAGList.begin();
    for(; it != graphNodeCDAGList.end(); ++it)
    {
      delete (*it); // remove all the graph nodes created
    }
  }

  GraphNode::NodeType convertType(int llvmtype){

    // The opcode enums can be found by looking at 
    // llvm::Instruction.cpp file

    switch(llvmtype){

      case 9:
        return (GraphNode::NODE_TYPE_FPADD);
      case 11:
        return (GraphNode::NODE_TYPE_FPSUB);
      case 13:
        return (GraphNode::NODE_TYPE_FPMUL);
      case 16:
        return (GraphNode::NODE_TYPE_FPDIV);
      case 27:
        return (GraphNode::NODE_TYPE_LOAD);
      default:
        return (GraphNode::NODE_TYPE_UNKNOWN);
    }
  }

  void init(const std::string& llvmBCFilename){
    llvm_shutdown_obj shutdownObj;  // Call llvm_shutdown() on exit.
    LLVMContext &context = getGlobalContext();

    SMDiagnostic err;
    auto_ptr<Module> module; 
    module.reset(ParseIRFile(llvmBCFilename, err, context));

    if (module.get() == 0) {
    //err.print(argv[0], errs());
     return;
    }

    Ids ids;
    ids.runOnModule(*module.get());

    const string programName = llvmBCFilename.substr(0, llvmBCFilename.find("."));

    clock_t begin = clock();
    DiskCDAG *cdag = DiskCDAG::generateGraph(ids, programName); 
    clock_t end = clock();
    double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
    cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;

    if(cdag)
    {
      //ofstream test("succtest");
      //cdag->printDiskGraph(test);
      //cdag->printGraph();

      //cdag->testMethodForDiskCache();
      begin = clock();
      //cdag->performBFSWithoutQ("bfsOut");
      //cdag->performBFS("bfsOut");
      cdag->printDOTGraph("diskgraph.dot");
      end = clock();
      elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
      cout << "\n Time taken for BFS traversal (in mins) : " << elapsed_time / 60;


      delete cdag;
    }
    else
    {
      std::cerr << "Fatal : Failed to generate CDAG \n";
    }
  }


private:
  list<GraphNode*> graphNodeCDAGList;
};