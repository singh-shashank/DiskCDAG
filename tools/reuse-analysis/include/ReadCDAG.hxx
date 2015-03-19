#include "GraphAnalysis.hxx"
#include "GraphNode.hxx"

#include "ddg/analysis/DiskCDAG.hxx"
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>



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

    DiskCDAG *cdag = DiskCDAG::generateGraph(ids, 512);

    if(cdag)
    {
      //cdag->printDiskGraph();
      cdag->flushCurrentBlockToDisk(true);

      // map<size_t,GraphNode*> idToGraphNodeMap; // maps dyn id to a GraphNode object

      // // Iterate over all the nodes creating a corresponding
      // // GraphNode object for each CDAG node
      // for(size_t gid=0; gid < cdag->getNumNodes(); ++gid)
      // {
      //   map<size_t,GraphNode*>::iterator nodeIt = idToGraphNodeMap.find(gid);
      //   GraphNode *curNode;

      //   if( nodeIt != idToGraphNodeMap.end() ){
      //     // GraphNode object already created for this object
      //     // get the reference to this node
      //     curNode = nodeIt->second;
      //     cout << "\n Node with gid : " << gid << " already created!";
      //   }
      //   else{
      //     // create a new GraphNode
      //     curNode = new GraphNode();
      //     curNode->setGlobalID(gid);
      //     curNode->setInstructionID(cdag->getStaticId(gid));
      //     curNode->setType(convertType(cdag->getType(gid)));

      //     // add node to map
      //     idToGraphNodeMap[gid] = curNode;

      //     // push node to GraphNode list
      //     graphNodeCDAGList.push_back(curNode);
      //   }



      //}


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