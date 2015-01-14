#ifndef RegionDynamicGraph_HXX
#define RegionDynamicGraph_HXX

#include "ddg/analysis/LazyGraph.hxx"

#include <llvm/Instruction.h>

#include <iostream>
#include <cstring>

//#include <vector>
#include <map>
#include <cassert>
#include <set>

namespace ddg
{
using namespace llvm;
using namespace std;

class RegionDynamicGraph;

struct RegionNode
{
	int id;
	vector<RegionNode*> succsList;
	vector<RegionNode*> predList;
  int ts;

	RegionNode(int id):id(id), ts(-1)
	{

	}

	void print()
	{
		cout <<"\n ---------------------------------------------------------";
		cout <<"\n Region Id : " << id;
    cout << "\nTimestep: " << ts;
		//cout <<"\n***************************************";
		cout <<"\n Successor List : ";
		for(vector<RegionNode*>::iterator it = succsList.begin();
			it != succsList.end(); ++it)
		{
			cout << (*it)->id << ", ";
		}
		cout <<"\n Pred List : ";
		for(vector<RegionNode*>::iterator it = predList.begin();
			it != predList.end(); ++it)
		{
			cout << (*it)->id << ", ";
		}
		//cout <<"\n ---------------------------------------------------------";		
	}

};

class RegionDynamicGraphBuilder : public LazyGraphVisitor<std::set<int> >
{
protected:
	RegionDynamicGraph *dynGraph;

public:
	RegionDynamicGraphBuilder(RegionDynamicGraph *dynGraph);
  	~RegionDynamicGraphBuilder();
	virtual void visitNode(LazyNode<std::set<int> > *node, Predecessors &predecessors, Operands &operands);
	virtual void visitRegionBegin();
	virtual void visitRegionEnd();
	int dynId;
	int regionId;
	bool regionHasInst;
	RegionNode *currNode;
	bool withinRegion;

	std::set<int> allPredsForRegion;
};



class RegionDynamicGraph
{
private:
	/*vector<Instruction*> nodes;
	vector<int> numSuccs;
	vector<int> numPreds;
	vector<int*> succList;
	vector<int*> predList;
	vector<Address> instAddr;*/
	RegionNode *root;
	Instruction **nodes;
	int *numSuccs;
	int *numPreds;
	int **succList;
	int **predList;
	Address *instAddr;
	unsigned long numNodes;
	unsigned long nodeCnt;

	

	RegionDynamicGraph(int numNodes); //Private constructor. Object of this class can be obtained only thru generateGraph method
	void addSuccessor(int nodeId, int* succs, int size);
	void addSuccessor(int nodeId, int succId);
	void addSuccessors();

	void getVfgPreds(RegionDynamicGraph *vfg, map<Address,int> &loadNode, int *preds, int predSize, int *nodeMap, int *vfgPredList, int &vfgPredSize);
	int getEffectivePred(int id, map<Address,int> &loadNode);
	//static RegionDynamicGraph *dynGraph;

public:
	~RegionDynamicGraph();
	//static RegionDynamicGraph* getInstance();

	RegionNode* getNewNode(int regionId)
	{
		return new RegionNode(regionId);
	}

	void addNodeToGraph(RegionNode *node)
	{
		if(!root)
		{
			root = node;
		}
		regionIdToNodeMap[node->id] = node;
		++numOfRegionNodes;
	}


	unsigned long getNumNodes();
	int addNode(Instruction *instr);
	int addNode(Instruction *instr, Address addr);
	void addPredecessor(int nodeId, int *preds, int size);
/*	void addPredecessors(int nodeId, Predecessors preds);*/
	//static RegionDynamicGraph *generateGraph(Ids& ids, int count);

	/*Templated function*/
	template <typename Builder>
	static RegionDynamicGraph* generateGraph(Ids& ids, int count)
	{
		RegionDynamicGraph *dynGraph = new RegionDynamicGraph(count);
		Builder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}

	/*Default template RegionDynamicGraphBuilder*/
	static RegionDynamicGraph* generateGraph(Ids& ids, int count)
	{		
		RegionDynamicGraph *dynGraph = new RegionDynamicGraph(count);
		RegionDynamicGraphBuilder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}


	//static RegionDynamicGraph *generateValueFlowGraph(Ids& ids);
	RegionDynamicGraph* extractValueFlowGraph();
	
	void printGraph();
	void printDOTGraph(const char *filename);
	void getSuccessors(int nodeId, int *&list, int &size);
	void getPredecessors(int nodeId, int *&list, int &size);
	int getNumSuccessors(int nodeId);
	int getNumPredecessors(int nodeId);
	Instruction *getNode(int nodeId);
	Address getAddress(int nodeId);
	
	void printRegionGraph();
	void performBFS(int &numEdges, int &noOfComp);
  void parallelism();

	vector<RegionNode*> regionIdToNodeMap;
	unsigned long numOfRegionNodes;
};

/*class ValueFlowGraphBuilder : public LazyGraphVisitor<vector<int> >
{
private:
	RegionDynamicGraph *dynGraph;
	int dynId;

	bool isSignificant(Instruction *instr);

public:
	ValueFlowGraphBuilder(RegionDynamicGraph *dynGraph);
	virtual void visitNode(LazyNode<vector<int> > *node, Predecessors &predecessors, Operands &operands);
};*/

}

#endif
