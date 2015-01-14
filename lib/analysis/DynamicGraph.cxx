#include <llvm/Instruction.h>

#include "ddg/analysis/DynamicGraph.hxx"

#include <iostream>
#include <cstring>

//#define DEBUG_DYNAMICGRAPH

#ifdef DEBUG_DYNAMICGRAPH
#include <iostream>
#define DEBUG_PRINT(arg) std::cout << "[DEBUG] " << arg << "\n"
#else
#define DEBUG_PRINT(arg)
#endif

namespace ddg
{
	
	//DynamicGraph *DynamicGraph::dynGraph = NULL;

	DynamicGraph::DynamicGraph(int count) : /*nodes(), numSuccs(), numPreds(), succList(), predList(),*/ numNodes(0), nodeCnt(count)
	{
		nodes = new Instruction*[nodeCnt];
		numSuccs = new int[nodeCnt];
		numPreds = new int[nodeCnt];
		succList = new int*[nodeCnt];
		predList = new int*[nodeCnt];
		instAddr = new Address[nodeCnt]();
	}

	DynamicGraph::~DynamicGraph()
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
	}

	/*DynamicGraph* DynamicGraph::getInstance()
	{
		if(dynGraph == NULL)
			dynGraph = new DynamicGraph();
					
		return dynGraph;
	}*/

	unsigned long DynamicGraph::getNumNodes()
	{
		return numNodes;
	}

	int DynamicGraph::addNode(Instruction *instr)
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

	int DynamicGraph::addNode(Instruction *instr, Address addr)
	{
		int ret = addNode(instr);
		//instAddr.back() = addr;
		instAddr[ret] = addr;
		return ret;
	}

	void DynamicGraph::addSuccessor(int nodeId, int* succs, int size)
	{
		assert(nodeId < numNodes);

		succList[nodeId] = succs;
		numSuccs[nodeId] = size;
	}

	inline void DynamicGraph::addSuccessor(int nodeId, int succId)
	{
		succList[nodeId][numSuccs[nodeId]] = succId;
		numSuccs[nodeId]++;
	}

	void DynamicGraph::addSuccessors()
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

	void DynamicGraph::addPredecessor(int nodeId, int *preds, int size)
	{
		assert(nodeId < numNodes);

		predList[nodeId] = preds;
		numPreds[nodeId] = size;

		//Set the successor count alone here. Successor list will be populated finally when the graph is generated. This is needed to get the successor count to dynamically allocate the successor list
		for(int i=0; i<size; i++)
			++numSuccs[preds[i]];
	}

	/*template <typename Builder=DynamicGraphBuilder>
	DynamicGraph* DynamicGraph::generateGraph(Ids& ids, int count)
	{
		DynamicGraph *dynGraph = new DynamicGraph(count);
		Builder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}*/

#if 0
	DynamicGraph* DynamicGraph::generateValueFlowGraph(Ids& ids)
	{
		DynamicGraph *dynGraph = new DynamicGraph;
		ValueFlowGraphBuilder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}
#endif

	void DynamicGraph::getSuccessors(int nodeId, int*& list, int &size)
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

	void DynamicGraph::getPredecessors(int nodeId, int*& list, int &size)
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

	int DynamicGraph::getNumSuccessors(int nodeId)
	{
		assert(nodeId < numNodes);
		return numSuccs[nodeId];
	}

	int DynamicGraph::getNumPredecessors(int nodeId)
	{
		assert(nodeId < numNodes);
		return numPreds[nodeId];
	}

	Instruction *DynamicGraph::getNode(int nodeId)
	{
		assert(nodeId < numNodes);
		return nodes[nodeId];
	}


	void DynamicGraph::printDOTGraph(const char *filename)
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

	void DynamicGraph::printGraph()
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

	DynamicGraphBuilder::DynamicGraphBuilder(DynamicGraph *dynGraph) : dynGraph(dynGraph), dynId(0)
	{
	}

  DynamicGraphBuilder::~DynamicGraphBuilder()
  {

    std::cout << "DynamicGraphBuilder destructor called : dynid: " << dynId << '\n';
    fflush(stdout);
  }

	void DynamicGraphBuilder::visitNode(LazyNode<int> *node, Predecessors &predecessors, Operands &operands)
	{
		int idx = 0;
		unsigned long nodeId;
		int predSize;
		int *predList;

		Instruction *instr = node->getInstruction();

		if(isa<LoadInst>(instr) || isa<StoreInst>(instr))
			nodeId = dynGraph->addNode(instr, node->getAddress());
		else
			nodeId = dynGraph->addNode(instr);
		predSize = predecessors.size();
		predList = new int[predSize];

		for(Predecessors::iterator pred = predecessors.begin(), predEnd = predecessors.end(); pred != predEnd;	++pred)
		{
			predList[idx++] = (*pred)->get();
		}
		dynGraph->addPredecessor(nodeId, predList, predSize);
		node->set(dynId);
		++dynId;
    //if(dynId % 1000 == 0)
    //{
    //std::cout << "dynid: " << dynId << '\n';
    //fflush(stdout);
    //}
	}

#if 0
	ValueFlowGraphBuilder::ValueFlowGraphBuilder(DynamicGraph *dynGraph) : dynGraph(dynGraph), dynId(0)
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

		for(Predecessors::iterator pred = predecessors.begin(), predEnd = predecessors.end(); pred != predEnd;	++pred)
		{
			predList[idx++] = (*pred)->getDynamicId();
		}
		dynGraph->addPredecessor(nodeId, predList, predSize);
	}
#endif

	DynamicGraph* DynamicGraph::extractValueFlowGraph()
	{
		int vfgNumNodes = 0;
		for(int i=0; i<numNodes; i++)
		{
			Instruction *instr = getNode(i);
			if((instr->isBinaryOp() && instr->getType()->isFloatingPointTy()) || isa<LoadInst>(instr))
				++vfgNumNodes;
		}
		DynamicGraph *vfg = new DynamicGraph(vfgNumNodes);
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

	inline void DynamicGraph::getVfgPreds(DynamicGraph* vfg, map<Address,int> &loadNode, int *preds, int predSize, int *nodeMap, int *vfgPredList, int &vfgPredSize)
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
	inline int DynamicGraph::getEffectivePred(int id, map<Address,int> &loadNode)
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

	Address DynamicGraph::getAddress(int nodeId)
	{
		return instAddr[nodeId];
	}
}
