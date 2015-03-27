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

  void init(const std::string& llvmBCFilename, 
    const std::string diskGraphFileName,
    const std::string diskGraphIndexFileName){
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

    size_t bs = 524288;// 4KB is the block size
    const string programName = llvmBCFilename.substr(0, llvmBCFilename.find("."));
    DiskCDAG *cdag = cdag = DiskCDAG::generateGraph(ids, programName, 
      diskGraphFileName, diskGraphIndexFileName, bs); 

    if(cdag)
    {
      //ofstream test("succtest");
      //cdag->printDiskGraph(test);
      //cdag->printGraph();
      

      

      //cdag->testMethodForDiskCache();
      cdag->performBFS();


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