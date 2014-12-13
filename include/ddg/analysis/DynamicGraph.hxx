#ifndef DYNAMICGRAPH_HXX
#define DYNAMICGRAPH_HXX

#include "ddg/analysis/LazyGraph.hxx"

//#include <vector>
#include <map>
#include <cassert>

namespace ddg
{
using namespace llvm;
using namespace std;

class DynamicGraph;

class DynamicGraphBuilder : public LazyGraphVisitor<int>
{
protected:
	DynamicGraph *dynGraph;

public:
	DynamicGraphBuilder(DynamicGraph *dynGraph);
  ~DynamicGraphBuilder();
	virtual void visitNode(LazyNode<int> *node, Predecessors &predecessors, Operands &operands);
	int dynId;
};

class DynamicGraph
{
private:
	/*vector<Instruction*> nodes;
	vector<int> numSuccs;
	vector<int> numPreds;
	vector<int*> succList;
	vector<int*> predList;
	vector<Address> instAddr;*/
	Instruction **nodes;
	int *numSuccs;
	int *numPreds;
	int **succList;
	int **predList;
	Address *instAddr;
	unsigned long numNodes;
	unsigned long nodeCnt;

	DynamicGraph(int numNodes); //Private constructor. Object of this class can be obtained only thru generateGraph method
	void addSuccessor(int nodeId, int* succs, int size);
	void addSuccessor(int nodeId, int succId);
	void addSuccessors();

	void getVfgPreds(DynamicGraph *vfg, map<Address,int> &loadNode, int *preds, int predSize, int *nodeMap, int *vfgPredList, int &vfgPredSize);
	int getEffectivePred(int id, map<Address,int> &loadNode);
	//static DynamicGraph *dynGraph;

public:
	~DynamicGraph();
	//static DynamicGraph* getInstance();
	unsigned long getNumNodes();
	int addNode(Instruction *instr);
	int addNode(Instruction *instr, Address addr);
	void addPredecessor(int nodeId, int *preds, int size);
/*	void addPredecessors(int nodeId, Predecessors preds);*/
	//static DynamicGraph *generateGraph(Ids& ids, int count);

	/*Templated function*/
	template <typename Builder>
	static DynamicGraph* generateGraph(Ids& ids, int count)
	{
		DynamicGraph *dynGraph = new DynamicGraph(count);
		Builder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}

	/*Default template DynamicGraphBuilder*/
	static DynamicGraph* generateGraph(Ids& ids, int count)
	{
		DynamicGraph *dynGraph = new DynamicGraph(count);
		DynamicGraphBuilder builder(dynGraph);

		builder.visit(ids);

		//Populate the successor list. Successor count is already set by addPredecessor()
		dynGraph->addSuccessors();

		return dynGraph;
	}


	//static DynamicGraph *generateValueFlowGraph(Ids& ids);
	DynamicGraph* extractValueFlowGraph();
	
	void printGraph();
	void printDOTGraph(const char *filename);
	void getSuccessors(int nodeId, int *&list, int &size);
	void getPredecessors(int nodeId, int *&list, int &size);
	int getNumSuccessors(int nodeId);
	int getNumPredecessors(int nodeId);
	Instruction *getNode(int nodeId);
	Address getAddress(int nodeId);
};

/*class ValueFlowGraphBuilder : public LazyGraphVisitor<vector<int> >
{
private:
	DynamicGraph *dynGraph;
	int dynId;

	bool isSignificant(Instruction *instr);

public:
	ValueFlowGraphBuilder(DynamicGraph *dynGraph);
	virtual void visitNode(LazyNode<vector<int> > *node, Predecessors &predecessors, Operands &operands);
};*/

}

#endif
