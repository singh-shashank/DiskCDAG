#ifndef CDAG_HXX
#define CDAG_HXX

#define FULL_DAG //If defined, whole ddg is built, else just the CDAG is built

#include "ddg/analysis/LazyGraph.hxx"
#include "CDAGCounter.hxx"

#include <map>
#include <set>
#include <climits>
#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <typeinfo>

namespace ddg
{
using namespace llvm;
using namespace std;

#ifdef FULL_DAG
typedef size_t payload_type;
#else
typedef std::set<size_t> payload_type;
#endif

class CDAGBuilder;
class CDAG;

class CDAGBuilder: public LazyGraphVisitor<payload_type>
{
	protected:
		CDAG *cdag;
		map<Address, size_t> loadMap; //Maps the memory address to ddg input node id

	public:
		CDAGBuilder(CDAG *cdag) :	cdag(cdag), loadMap() {}

		virtual void visitNode(LazyNode<payload_type> *node, Predecessors	&predecessors, Operands &operands);
};

class CDAG
{
	private:
		size_t numNodes; //No. of nodes created so far
		size_t count; //Total expected no. of nodes
		size_t *predCnt; //Array containing predecessor count of each node
		size_t *scsrCnt; //Array containing successor count of each node
		size_t **predList; //2D array pointing to array of predecessor ids of each node
		size_t **scsrList; //2D array pointing to array of successor ids of each node
		size_t *staticId; //Array containing static id of each node
		unsigned int *type; //Array containing instruction type of each node

		void init(size_t count)
		{
			predCnt = new size_t[count]();
			scsrCnt = new size_t[count]();
			predList = new size_t*[count]();
			scsrList = new size_t*[count]();
			staticId = new size_t[count];
			type = new unsigned int[count];
		}

		//Adds the successor 'scsrId' to node 'nodeId'
		void addSuccessor(size_t nodeId, size_t scsrId)
		{
			scsrList[nodeId][scsrCnt[nodeId]++] = scsrId;
		}

	public:
		CDAG(size_t count) : numNodes(0), count(count)
		{
			init(count);
		}

		CDAG(Ids &ids) : numNodes(0)
		{
			CDAGCounter counter(ids); //Get the count of expected no. of nodes
			count = counter.getCount();
			init(count);
		}

		~CDAG()
		{
			for(size_t i=0; i<numNodes; ++i)
			{
				delete [](predList[i]);
				delete [](scsrList[i]);
			}
			delete []predCnt;
			delete []scsrCnt;
			delete []predList;
			delete []scsrList;
			delete []type;
			delete []staticId;
		}

		//Returns the no. of nodes in the cdag
		size_t getNumNodes()
		{
			return numNodes;
		}

		//Adds a node to the cdag with instruction type 'currType' and static id 'id'
		size_t addNode(unsigned int currType, size_t id)
		{
			assert(numNodes < count);
			/*predCnt[numNodes] = 0;
			scsrCnt[numNodes] = 0;
			predList[numNodes] = NULL;
			scsrList[numNodes] = NULL;*/
			type[numNodes] = currType;
			staticId[numNodes] = id;

			return numNodes++;
		}

		//Sets the predecessor list for the node 'nodeId'
		void setPredecessor(size_t nodeId, size_t *preds, size_t size)
		{
			assert(nodeId < numNodes);
			predList[nodeId] = preds;
			predCnt[nodeId] = size;

			/*Set the successor count alone here. Successor list will be
			 * populated finally when the graph is generated. This is needed
			 * to get the successor count to dynamically allocate the
			 * successor list*/
			for(size_t i=0; i<size; ++i)
				++scsrCnt[preds[i]];
		}

		void setPredecessor(size_t nodeId, const set<size_t> &predSet)
		{
			assert(nodeId < numNodes);
			size_t size = predSet.size();
			size_t *list = new size_t[size];
			set<size_t>::const_iterator it = predSet.begin();
			for(size_t i=0; i<size; ++i)
			{
				list[i] = *it;
				++it;
			}
			setPredecessor(nodeId, list, size);
		}

		//Returns the successor list of 'nodeId'
		void getSuccessors(size_t nodeId, const size_t *&list, size_t &size)
		{
			assert(nodeId < numNodes);
			size = scsrCnt[nodeId];
			/*if(size == 0)
			{
				list = NULL;
				return;
			}*/
			list = scsrList[nodeId];
		}

		//Returns the predecessor list of 'nodeId'
		void getPredecessors(size_t nodeId, const size_t *&list, size_t &size)
		{
			assert(nodeId < numNodes);
			size = predCnt[nodeId];
			/*if(size == 0)
			{
				list = NULL;
				return;
			}*/
			list = predList[nodeId];
		}

		//Returns no. of successors of 'nodeId'
		size_t getNumSuccessors(size_t nodeId)
		{
			assert(nodeId < numNodes);
			return scsrCnt[nodeId];
		}

		//Returns no. of predecessors of 'nodeId'
		size_t getNumPredecessors(size_t nodeId)
		{
			assert(nodeId < numNodes);
			return predCnt[nodeId];
		}


		//Returns True if 'nodeId' is input node, false otherwise
		bool isInputNode(size_t nodeId)
		{
			//Currently doesn't support input tagging. Any node with zero predecessors are considered as inputs.
			if(predCnt[nodeId] == 0 )
				return true;
			return false;
		}

		//Returns True if 'nodeId' is output node, false otherwise
		bool isOutputNode(size_t nodeId)
		{
			//Currently doesn't support output tagging. Any node with zero successors are considered as outputs.
			if(scsrCnt[nodeId] == 0)
				return true;
			return false;
		}

		//Populates successor list for all the nodes in the cdag using the predecessor info.
		void setSuccessors()
		{
			for(size_t i=0; i<numNodes; ++i)
			{
				scsrList[i] = new size_t[scsrCnt[i]];
				scsrCnt[i] = 0; //Reset the count to zero. This will be updated later by addSuccessor().
			}
			for(size_t i=0; i<numNodes; ++i)
			{
				size_t numPreds = predCnt[i];
				for(size_t j=0; j<numPreds; ++j)
				{
					addSuccessor(predList[i][j], i);
				}
			}
		}

		//Generates the CDAG using CDAGBuilder
		static CDAG* generateGraph(Ids& ids)
		{
			CDAG *cdag = new CDAG(ids);
			CDAGBuilder builder(cdag);

			builder.visit(ids);

			//Populate the successor list. Successor count has been already set by setPredecessor()
			cdag->setSuccessors();

			return cdag;
		}

		//Generates the CDAG using user-specified 'Builder'
		template <typename Builder>
			static CDAG* generateGraph(Ids& ids)
			{
				CDAG *cdag = new CDAG(ids);
				Builder builder(cdag);

				builder.visit(ids);

				//Populate the successor list. Successor count is already set by setPredecessor()
				cdag->setSuccessors();

				return cdag;
			}

		//Returns the instruction type of 'nodeId'
		unsigned int getType(size_t nodeId)
		{
			assert(nodeId < numNodes);
			return type[nodeId];
		}

		//Returns static id of 'nodeId'
		unsigned int getStaticId(size_t nodeId)
		{
			assert(nodeId < numNodes);
			return staticId[nodeId];
		}

		//Pretty prints the graph in text format
		void printGraph()
		{
			std::cout << numNodes << '\n';
			for(size_t i=0; i<numNodes; ++i)
			{
				std::cout << i << ". Instruction: " << llvm::Instruction::getOpcodeName(type[i]) << "; StaticID: " << staticId[i] << "\n";
				size_t numPreds = predCnt[i];
				std::cout << "num predecessors: " << numPreds << "\n";
				for(size_t j=0; j<numPreds; j++)
				{
					std::cout << predList[i][j] << '\t';
				}
				std::cout << '\n';
				size_t numScsrs = scsrCnt[i];
				std::cout << "num successors: " << numScsrs << '\n';
				for(size_t j=0; j<numScsrs; j++)
				{
					std::cout << scsrList[i][j] << '\t';
				}
				std::cout << "\n------------------------------------------------------\n";
			}
		}

		//Prints the graph in dot format
		void printDOTGraph(const char *filename)
		{
			ofstream file;
			file.open(filename);
			file << "digraph \"ddg\" {\n";

			for(size_t i=0; i<numNodes; i++)
			{
				file << "\tNode" << i << " [label=\"" << i << ". " << llvm::Instruction::getOpcodeName(type[i]) << "\"];\n";
				size_t numPreds = predCnt[i];
				for(size_t j=0; j<numPreds; j++)
				{
					file << "\tNode" << predList[i][j] << " -> Node" << i << ";\n";
				}
			}

			file << "}";
			file.close();
		}

		//Prints the graph in YAML format
		void printYAMLGraph(const char *filename)
		{
			ofstream file;
			file.open(filename);

			file << "Nodes: \n";
			for(size_t i=0; i<numNodes; ++i)
			{
				file << "  " << i << ": {null: 0}\n";
			}
			file << "Edges: \n";
			for(size_t i=0; i<numNodes; ++i)
			{
				size_t numScsrs = scsrCnt[i];
				if(numScsrs == 0)
					continue;
				file << "  " << i << ": {";
				for(size_t j=0; j<numScsrs-1; ++j)
				{
					file << scsrList[i][j] << ": 1, ";
				}
				file << scsrList[i][numScsrs-1] << ": 1}\n";
			}
			file.close();
		}
};

#ifdef FULL_DAG
void CDAGBuilder::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
{
	set<size_t> predSet;
	Instruction *instr = node->getInstruction();

	//In case of LoadInst, check if we need to create an edge from input node
	if(isa<LoadInst>(instr))
	{
		bool flag = true;
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			if(isa<StoreInst>((*pred)->getInstruction()))
			{
				flag = false;
				break;
			}
		}
		if(flag == true)
		{
			//Check if a cdag node with the same address has been already created. If so, do nothing; else create a new input node.
			Address addr = node->getAddress();
			std::map<Address, size_t>::const_iterator it = loadMap.find(addr);
			size_t tempId;
			if(it == loadMap.end())
			{
				size_t inputNodeId = cdag->addNode(Instruction::Load, cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
				loadMap.insert(make_pair(addr, inputNodeId));
				tempId = inputNodeId;
			}
			else
			{
				tempId = it->second;
			}
			predSet.insert(tempId);
		}
	}

	//Create a cdag node
	size_t nodeId = cdag->addNode(instr->getOpcode(), cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
	for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
	{
		payload_type pred_data = (*pred)->get();
		predSet.insert(pred_data);
	}
	cdag->setPredecessor(nodeId, predSet);
	node->set(nodeId);
}
#else
void CDAGBuilder::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
{
	set<size_t> predSet;
	Instruction *instr = node->getInstruction();

	//In case of LoadInst, check if we need to create an input node.
	if(isa<LoadInst>(instr))
	{
		bool flag = true; //false if the load is preceded by a store.
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			if(isa<StoreInst>((*pred)->getInstruction()))
			{
				flag = false;
				break;
			}
		}
		if(flag == true)
		{
			//Check if a cdag node with the same address has been already created. If so, do nothing; else create a new input node.
			Address addr = node->getAddress();
			std::map<Address, size_t>::const_iterator it = loadMap.find(addr);
			size_t tempId;
			if(it == loadMap.end())
			{
				size_t inputNodeId = cdag->addNode(Instruction::Load, cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
				loadMap.insert(make_pair(addr, inputNodeId));
				tempId = inputNodeId;
			}
			else
			{
				tempId = it->second;
			}
			node->get().clear();
			node->get().insert(tempId);
			return;
		}
	}

	if(instr->isBinaryOp() && instr->getType()->isFloatingPointTy())
	{
		//Create a cdag node
		size_t nodeId = cdag->addNode(instr->getOpcode(), cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			payload_type pred_data = (*pred)->get();
			predSet.insert(pred_data.begin(), pred_data.end());
		}
		cdag->setPredecessor(nodeId, predSet);
		node->get().clear();
		node->get().insert(nodeId);
	}
	else
	{
		set<size_t> node_data;
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			set<size_t> &pred_data = (*pred)->get();
			node_data.insert(pred_data.begin(), pred_data.end());
		}
		node->get().clear();
		node->get().insert(node_data.begin(), node_data.end());
	}
}
#endif

}

#endif