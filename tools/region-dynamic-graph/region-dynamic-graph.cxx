#include "ddg/analysis/DynInstCounter.hxx"
#include "region-dynamic-graph.hxx"

#include <llvm/LLVMContext.h>
#include <llvm/Constants.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>


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

  DynInstCounter counter(ids);
  int count = counter.getCount();
  std::cout << " Instruction count = " << count << '\n';
  DynInstCounter counter2(ids);
  int regionCount = counter2.getRegionCount();
  std::cout << "region count  = " << regionCount << '\n';
  RegionDynamicGraph *dynGraph = RegionDynamicGraph::generateGraph(ids, count);
  cout << "\n";
  //dynGraph->printRegionGraph();
  dynGraph->parallelism();
  cout << "\n";

  int numEdges = 0;
  int numOfComp = 0;
  dynGraph->performBFS(numEdges, numOfComp);
  cout << "\n Actual Number of Region Nodes = " << dynGraph->numOfRegionNodes << '\n';
  cout << "\n Total number of edges = " << numEdges;
  cout << "\n Total number of components = " << numOfComp;

  //dynGraph->printGraph();
  //dynGraph->printDOTGraph("dyn_graph.dot");
  //std::cout << "count: " << count << "\n";
  //std::cout << "numnodes: " << dynGraph->getNumNodes() << "\n";

  delete dynGraph;
  cout << '\n';
}



//#define DEBUG_RegionDynamicGraph

#ifdef DEBUG_RegionDynamicGraph
#include <iostream>
#define DEBUG_PRINT(arg) std::cout << "[DEBUG] " << arg << "\n"
#else
#define DEBUG_PRINT(arg)
#endif

namespace ddg
{
  
  //RegionDynamicGraph *RegionDynamicGraph::dynGraph = NULL;

  RegionDynamicGraph::RegionDynamicGraph(int count) : /*nodes(), numSuccs(), numPreds(), succList(), predList(),*/ numNodes(0), nodeCnt(count)
  {
    nodes = new Instruction*[nodeCnt];
    numSuccs = new int[nodeCnt];
    numPreds = new int[nodeCnt];
    succList = new int*[nodeCnt];
    predList = new int*[nodeCnt];
    instAddr = new Address[nodeCnt]();

    regionIdToNodeMap.resize(nodeCnt, NULL);

    numOfRegionNodes = 0;
    root = NULL;
  }

  RegionDynamicGraph::~RegionDynamicGraph()
  {
    for(int i=0; i<numNodes; i++)
    {
      delete[] succList[i];
      delete[] predList[i];
      //dynGraph = NULL;
    }
    delete[] nodes;
    delete[] numSuccs;
    delete[] numPreds;
    delete[] succList;
    delete[] predList;
    delete[] instAddr;

    for(int i=0; i<nodeCnt; ++i)
    {
      if(regionIdToNodeMap[i])
      {
        delete regionIdToNodeMap[i];
      }
    }

  }

  /*RegionDynamicGraph* RegionDynamicGraph::getInstance()
  {
    if(dynGraph == NULL)
      dynGraph = new RegionDynamicGraph();
          
    return dynGraph;
  }*/

  unsigned long RegionDynamicGraph::getNumNodes()
  {
    return numNodes;
  }

  int RegionDynamicGraph::addNode(Instruction *instr)
  {
    assert(numNodes < nodeCnt);

    /*nodes.push_back(instr);
    numSuccs.push_back(0);
    numPreds.push_back(0);
    succList.push_back(NULL);
    predList.push_back(NULL);
    instAddr.push_back(0);*/
    nodes[numNodes] = instr;
    numSuccs[numNodes] = 0;
    numPreds[numNodes] = 0;
    succList[numNodes] = NULL;
    predList[numNodes] = NULL;

    return numNodes++;
  }

  int RegionDynamicGraph::addNode(Instruction *instr, Address addr)
  {
    int ret = addNode(instr);
    //instAddr.back() = addr;
    instAddr[ret] = addr;
    return ret;
  }

  void RegionDynamicGraph::addSuccessor(int nodeId, int* succs, int size)
  {
    assert(nodeId < numNodes);

    succList[nodeId] = succs;
    numSuccs[nodeId] = size;
  }

  inline void RegionDynamicGraph::addSuccessor(int nodeId, int succId)
  {
    succList[nodeId][numSuccs[nodeId]] = succId;
    numSuccs[nodeId]++;
  }

  void RegionDynamicGraph::addSuccessors()
  { 
    for(int i=0; i<numNodes; i++)
    {
      succList[i] = new int[numSuccs[i]];
      numSuccs[i] = 0;
    }
    for(int i=0; i<numNodes; i++)
    {
      for(int j=0; j<numPreds[i]; j++)
      {
        addSuccessor(predList[i][j], i);
      }
    }
  }

  void RegionDynamicGraph::addPredecessor(int nodeId, int *preds, int size)
  {
    assert(nodeId < numNodes);

    predList[nodeId] = preds;
    numPreds[nodeId] = size;

    //Set the successor count alone here. Successor list will be populated finally when the graph is generated. This is needed to get the successor count to dynamically allocate the successor list
    for(int i=0; i<size; i++)
      ++numSuccs[preds[i]];
  }

  /*template <typename Builder=RegionDynamicGraphBuilder>
  RegionDynamicGraph* RegionDynamicGraph::generateGraph(Ids& ids, int count)
  {
    RegionDynamicGraph *dynGraph = new RegionDynamicGraph(count);
    Builder builder(dynGraph);

    builder.visit(ids);

    //Populate the successor list. Successor count is already set by addPredecessor()
    dynGraph->addSuccessors();

    return dynGraph;
  }*/

#if 0
  RegionDynamicGraph* RegionDynamicGraph::generateValueFlowGraph(Ids& ids)
  {
    RegionDynamicGraph *dynGraph = new RegionDynamicGraph;
    ValueFlowGraphBuilder builder(dynGraph);

    builder.visit(ids);

    //Populate the successor list. Successor count is already set by addPredecessor()
    dynGraph->addSuccessors();

    return dynGraph;
  }
#endif

  void RegionDynamicGraph::getSuccessors(int nodeId, int*& list, int &size)
  {
    assert(nodeId < numNodes);
    size = numSuccs[nodeId];
    if(size == 0)
    {
      list = NULL;
      return;
    }
    list = new int[size];
    memcpy(list, succList[nodeId], sizeof(int) * size);
  }

  void RegionDynamicGraph::getPredecessors(int nodeId, int*& list, int &size)
  {
    assert(nodeId < numNodes);
    size = numPreds[nodeId];
    if(size == 0)
    {
      list = NULL;
      return;
    }
    list = new int[size];
    memcpy(list, predList[nodeId], sizeof(int) * size);
  }

  int RegionDynamicGraph::getNumSuccessors(int nodeId)
  {
    assert(nodeId < numNodes);
    return numSuccs[nodeId];
  }

  int RegionDynamicGraph::getNumPredecessors(int nodeId)
  {
    assert(nodeId < numNodes);
    return numPreds[nodeId];
  }

  Instruction *RegionDynamicGraph::getNode(int nodeId)
  {
    assert(nodeId < numNodes);
    return nodes[nodeId];
  }


  void RegionDynamicGraph::printDOTGraph(const char *filename)
  {
    ofstream file;
    file.open(filename);
    file << "digraph \"ddg\" {\n";

    for(int i=0; i<numNodes; i++)
    {
      file << "\tNode" << i << " [label=\"" << i << ". " << nodes[i]->getOpcodeName() << "\"];\n";
      for(int j=0; j<numPreds[i]; j++)
      {
        file << "\tNode" << predList[i][j] << " -> Node" << i << ";\n";
      }
    }

    file << "}";
    file.close();
  }

  void RegionDynamicGraph::printGraph()
  {
    std::cout << numNodes << "\n";
    for(int i=0; i<numNodes; i++)
    {
      Id id= cast<ConstantInt>(nodes[i]->getMetadata("id")->getOperand(0))->getZExtValue();
      std::cout << i << ". " << nodes[i]->getOpcodeName() << " (id = " << id << ")\n";
      std::cout << "num predecessors: " << numPreds[i] << "\n";
      for(int j=0; j<numPreds[i]; j++)
      {
        std::cout << predList[i][j] << "\t";
      }
      std::cout << "\n";
      std::cout << "num successors: " << numSuccs[i] << "\n";
      for(int j=0; j<numSuccs[i]; j++)
      {
        //std::cout << nodes[succList[i][j]]->getOpcodeName() << "\t";
        std::cout << succList[i][j] << "\t";
      }
      std::cout << "\n------------------------------------------------------\n";
    }
  }

  void RegionDynamicGraph::performBFS(int &numEdges, int &noOfComp)
  {
    std::map<int, bool> visited;

    for(int i=0; i<nodeCnt; ++i)
    {
      if(regionIdToNodeMap[i])
      {
        visited[i] = false;
      }
    }

    RegionNode *startNode = root;
    while(startNode)
    {
      //cout << "\n Start Node : " << startNode->id;
      ++noOfComp;
      int edges = 0;
      int nodes = 0;
      fflush(stdout);
      std::queue<RegionNode*> q;
      q.push(startNode);
      visited[startNode->id] = true;

      while(!q.empty())
      {
        RegionNode *temp = q.front();
        q.pop();
        //cout << "id: " << temp->id << '\n';
        ++nodes;
        numEdges += temp->succsList.size();

        for(std::vector<RegionNode*>::iterator it = temp->succsList.begin();
          it != temp->succsList.end(); ++it)
        {
          std::map<int, bool>::iterator it2 = visited.find((*it)->id);
          if(it2 != visited.end() && !it2->second)
          {
            q.push((*it));
            it2->second = true;
            //++numEdges;
            ++edges;
          }
          else if(it2 == visited.end())
          {
            cout << "\n Unexpected : BFS Traversal";
          }
        }
      }

      //cout << " Nodes : " << nodes << " Edges : " << edges;
      startNode = NULL;
      for(std::map<int, bool>::iterator it = visited.begin();
        it != visited.end(); ++it)
      {
        if(!it->second)
        {
          startNode = regionIdToNodeMap[it->first];
          break;
        }
      }
    }
  }

  void RegionDynamicGraph::printRegionGraph()
  {
    for(int i=0; i<nodeCnt; ++i)
    {
      //try {
      //  currNode = regionIdToNodeMap.at(i);
      //}
      //catch(const std::out_of_range& oor) {
      //  cerr << "Error: regionIdToNodeMap out of range. NodeID: " << i << "; nodeCnt: " << nodeCnt << '\n';
      //  return;
      //}
      if(regionIdToNodeMap[i])
      {
        regionIdToNodeMap[i]->print();
      }
    }
  }

  void RegionDynamicGraph::parallelism()
  {
    int maxTs = 0;
    RegionNode *currNode;
    for(int i=0; i<nodeCnt; ++i)
    {
      //try {
      //  currNode = regionIdToNodeMap.at(i);
      //}
      //catch(const std::out_of_range& oor) {
      //  cerr << "Error: regionIdToNodeMap out of range. NodeID: " << i << "; nodeCnt: " << nodeCnt << '\n';
      //  return;
      //}
      if(regionIdToNodeMap[i])
      {
        currNode = regionIdToNodeMap[i];
        int currTs = currNode->ts;
        if(currTs > maxTs)
          maxTs = currTs;
      }
    }
    cout << "\nTotal nodes: " << numOfRegionNodes << "; Critical path size: " << maxTs+1 << "; Available parallelism: " << ((double)(numOfRegionNodes))/((double)(maxTs+1)) << '\n';
  }

  RegionDynamicGraphBuilder::RegionDynamicGraphBuilder(RegionDynamicGraph *dynGraph) : 
  dynGraph(dynGraph), dynId(0), regionId(-1), regionHasInst(false),
  withinRegion(false)
  {
  }

  RegionDynamicGraphBuilder::~RegionDynamicGraphBuilder()
  {

    std::cout << "RegionDynamicGraphBuilder destructor called : dynid: " << dynId << '\n';
    fflush(stdout);
  }

  void RegionDynamicGraphBuilder::visitRegionBegin()
  {
    regionHasInst = false;
    withinRegion = true;
    ++regionId;
    currNode = dynGraph->getNewNode(regionId);
    allPredsForRegion.clear();
    //cout << "\n Starting Region " << regionId;
  }

  void RegionDynamicGraphBuilder::visitRegionEnd()
  {
    withinRegion = false;
    if(!regionHasInst)
    {
      delete currNode;
    }
    else
    {
      // Create new edges in the graph
      std::set<int>::iterator it = allPredsForRegion.begin();
      //cout << "\nRegion " << regionId << " : allPredsSize = " <<allPredsForRegion.size();
      int currTs = 0;
      for(; it != allPredsForRegion.end(); ++it)
      {

        if((*it) == regionId)
          continue;
        RegionNode *predNode = dynGraph->regionIdToNodeMap[(*it)];
        if(predNode)
        {
          predNode->succsList.push_back(currNode);
          currNode->predList.push_back(predNode);
          int predTs = predNode->ts;
          if(predTs > currTs-1)
            currTs = predTs+1;
        }
        else
        {
          cout << "\n Unexpected Error :  A predecessor with null node pointer ";
        }
      }
      currNode->ts = currTs;
      dynGraph->addNodeToGraph(currNode);
    }
    //cout << "\n Ending Region " << regionId;
  }

  void RegionDynamicGraphBuilder::visitNode(LazyNode<std::set<int> > *node, Predecessors &predecessors, Operands &operands)
  {
    //cout <<"\n==========\n";
    int idx = 0;
    unsigned long nodeId;
    int predSize;
    int *predList;

    regionHasInst = true;

    Instruction *instr = node->getInstruction();

    // if(isa<LoadInst>(instr) || isa<StoreInst>(instr))
    //   nodeId = dynGraph->addNode(instr, node->getAddress());
    // else
    //   nodeId = dynGraph->addNode(instr);
    predSize = predecessors.size();
    //predList = new int[predSize];

    std::set<int> predRegionIdSet;
    //cout << "preds:\n";
    //int max_size=0;
    for(Predecessors::iterator pred = predecessors.begin(), predEnd = predecessors.end(); pred != predEnd;  ++pred)
    {
      //predList[idx++] = (*pred)->get();
      
      predRegionIdSet.insert((*pred)->get().begin(), (*pred)->get().end());
      //int sz = (*pred)->get().size();
      //max_size = (sz>max_size) ? sz : max_size;

      
    //cout << cast<ConstantInt>((*pred)->getInstruction()->getMetadata("id")->getOperand(0))->getZExtValue() << ": ";
    //   for(std::set<int>::iterator it = (*pred)->get().begin();
    //     it != (*pred)->get().end(); ++it)
    //   {
    //     cout << (*it) << ", ";
    //   }
    //   cout << '\n';
    }
    //cout << "----\n";

    // If we are outside a region then Lazy Node set should be re-created
    // merging all the predeccessor
    node->get().clear();
    if(withinRegion)
    {
    //cout << "\nRegion id " << cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue() << ":" << regionId << '\n'; 
      allPredsForRegion.insert(predRegionIdSet.begin(), predRegionIdSet.end());
      node->get().insert(regionId);
    }
    else 
    {
      //cout << "size b4: " << node->get().size() << "\n";
      //cout << "\nRegion id " << cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue() << ":" << "-2\n"; 
      node->get().insert(predRegionIdSet.begin(), predRegionIdSet.end());
      //cout << "pred size: " << max_size << "\n";
      //cout << "size after: " << node->get().size() << "\n";
    }

    // if(regionId != -1 && predRegionIdSet.size() > 0) // check if we haven't encountered a region yet
    // {
    //   node->get().insert(regionId);
    // }

    //dynGraph->addPredecessor(nodeId, predList, predSize);
    //node->set(dynId);
    ++dynId;
    /*if(dynId % 1000 == 0)
    {
    std::cout << "dynid: " << dynId << '\n';
    fflush(stdout);
    }*/


  }

#if 0
  ValueFlowGraphBuilder::ValueFlowGraphBuilder(RegionDynamicGraph *dynGraph) : dynGraph(dynGraph), dynId(0)
  {
  }

  inline bool ValueFlowGraphBuilder::isSignificant(Instruction *instr)
  {
    unsigned int valueId = instr->getValueID();
    if(valueId == 31 || valueId == 33 || valueId == 35 || valueId == 38 || valueId == 49 || valueId == 50)
      return true;
    return false;
  }

  void ValueFlowGraphBuilder::visitNode(LazyNode<vector<int> > *node, Predecessors &predecessors, Operands &operands)
  {
    int idx = 0;
    unsigned long nodeId;
    int predSize;
    int *predList;

    Instruction *instr = node->getInstruction();

    //std::cout << instr->getOpcodeName() << "\t" << instr->getValueID() << "\n";
    //return;

    if(!isSignificant(instr))
    {
      node->setDynamicId(dynId);
      return;
    }

    //node->setDynamicId(++dynId);
    nodeId = dynGraph->addNode(instr);
    dynId = (dynGraph->getNumNodes())-1;
    node->setDynamicId(dynId);
    predSize = predecessors.size();
    predList = new int[predSize];

    for(Predecessors::iterator pred = predecessors.begin(), predEnd = predecessors.end(); pred != predEnd;  ++pred)
    {
      predList[idx++] = (*pred)->getDynamicId();
    }
    dynGraph->addPredecessor(nodeId, predList, predSize);
  }
#endif

  RegionDynamicGraph* RegionDynamicGraph::extractValueFlowGraph()
  {
    int vfgNumNodes = 0;
    for(int i=0; i<numNodes; i++)
    {
      Instruction *instr = getNode(i);
      if((instr->isBinaryOp() && instr->getType()->isFloatingPointTy()) || isa<LoadInst>(instr))
        ++vfgNumNodes;
    }
    RegionDynamicGraph *vfg = new RegionDynamicGraph(vfgNumNodes);
    int *nodeMap = new int[numNodes]; //Maps the node id of original dynamic graph to the node id of vfg
    map<Address, int> loadNode;

    for(int i=0; i<numNodes; i++)
      nodeMap[i] = -1;

    for(int i=0; i<numNodes; i++)
    {
      Instruction *instr = getNode(i);
      if(!(instr->isBinaryOp() && instr->getType()->isFloatingPointTy()))
        continue;

      DEBUG_PRINT("------------------------------------");
      DEBUG_PRINT("Adding VFG node: " << i << ". " << instr->getOpcodeName());

      int *preds;
      int predSize;
      int *vfgPredList;
      int vfgNodeId;
      int vfgPredSize;
      vfgPredList = new int[predSize];
      getPredecessors(i, preds, predSize);
      getVfgPreds(vfg, loadNode, preds, predSize, nodeMap, vfgPredList, vfgPredSize);
      vfgNodeId = vfg->addNode(instr);
      nodeMap[i] = vfgNodeId;
      vfg->addPredecessor(vfgNodeId, vfgPredList, vfgPredSize);
      delete[] preds;
    }
    vfg->addSuccessors();

    return vfg;
  }

  inline void RegionDynamicGraph::getVfgPreds(RegionDynamicGraph* vfg, map<Address,int> &loadNode, int *preds, int predSize, int *nodeMap, int *vfgPredList, int &vfgPredSize)
  {
    vfgPredSize = 0;
    for(int i=0; i<predSize; i++)
    {
      int effPred;
      int predId;
      effPred = getEffectivePred(preds[i], loadNode);
      if(effPred == -1) //Case where operand is a constant
        continue;
      predId = nodeMap[effPred];
      if(predId == -1)
      {
        predId = vfg->addNode(getNode(effPred));
        nodeMap[effPred] = predId;
      }
      vfgPredList[vfgPredSize++] = predId;
    }
  }

  //Recursively finds the correctpredecessor to be substituted for the actual predecessor
  inline int RegionDynamicGraph::getEffectivePred(int id, map<Address,int> &loadNode)
  {
    int *predList;
    int size;
    Instruction *instr = getNode(id);

    DEBUG_PRINT("Predecessor: " << instr->getOpcodeName());

    if(instr->isBinaryOp() && instr->getType()->isFloatingPointTy())
      return id;
    else if(isa<LoadInst>(instr) && (getNumPredecessors(id) == 1)) //Load vertex with only getelementptr as pred. This load address is not preceded by store, but can be preceded by another load (RAR dependence)
    {
      Address addr = instAddr[id];
      DEBUG_PRINT("Load address: " << addr);
      map<Address, int>::iterator it = loadNode.find(addr);
      if(it != loadNode.end())
        return it->second;
      loadNode[addr] = id;
      return id;
    }
    else
    {
      getPredecessors(id, predList, size);
      if(size == 0) //Case where operand is a constant
        return -1; 
      if(isa<LoadInst>(instr)) //Load vertex, preceded by a store
      {
        //Recursively get effective pred by following the predecessor store instruction
        for(int i=0; i<size; i++)
          if(isa<StoreInst>(getNode(predList[i])))
            return getEffectivePred(predList[i], loadNode);
      }
      else if(isa<StoreInst>(instr))
      {
        assert(size == 2);
        //Recursively get effective pred by following the predecessor that generated the value
        for(int i=0; i<size; i++)
          if(!isa<GetElementPtrInst>(getNode(predList[i])))
            return getEffectivePred(predList[i], loadNode);
      }
      else
      {
        assert(size == 1);
        return getEffectivePred(predList[0], loadNode);
      }
    }

  }

  Address RegionDynamicGraph::getAddress(int nodeId)
  {
    return instAddr[nodeId];
  }
}
