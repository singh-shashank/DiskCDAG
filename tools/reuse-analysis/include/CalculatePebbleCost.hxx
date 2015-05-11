#ifndef CALCULATE_PEBBLE_COST
#define CALCULATE_PEBBLE_COST

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>

#include "ddg/analysis/DiskCDAG.hxx"

using namespace std;
using namespace llvm;


namespace ddg
{
template <typename dag>
inline void spill(dag *cdag, Id numNodes, set<Id> &predSet, set<Id> &redPblNodes, const Id *loc, bool *dirty, int &remPbls, unsigned long &cost, Id currTS /*current timestamp*/)
{
	set<Id>::const_iterator predSetEnd = predSet.end();
	Id pos = 0, spillNode;
	for(set<Id>::const_iterator it = redPblNodes.begin(), itEnd = redPblNodes.end(); it != itEnd; ++it)
	{
		Id currNode = *it;
		// cout << "analyzing " << currNode << "\n";
		if(predSet.find(currNode) != predSetEnd)
			continue;
		Id numSuccs;
		Id tempPos = numNodes;
		numSuccs = cdag->getNode(currNode)->succsList.size();
		//cout << "scsrs: " << numSuccs << "\n";
		assert(numSuccs > 0);
		for(Id i=0; i<numSuccs; ++i)
		{
			Id temp = loc[cdag->getNode(currNode)->succsList[i]];
			//cout << "nodeid: " << currNode << " temp: " << temp << " TS: " << currTS << "\n";
			if((temp < tempPos) && (temp > currTS)) //position of nearest unfired successor
				tempPos = temp;
		}
		// cout.flush();
		assert(tempPos != numNodes);
		if(tempPos > pos)
		{
			pos = tempPos;
			spillNode = currNode;
		}
	}
	assert(pos != 0);
	redPblNodes.erase(spillNode);
	++remPbls;
	if(dirty[spillNode] == true)
	{
		++cost;
		dirty[spillNode] = false;
	}
}

template <typename dag>
unsigned long calcPblCost(dag *cdag, Id *finalSchedule, Id K, Id numNodes = 0)
{
	if(numNodes == 0)
		numNodes = cdag->getNumNodes();
	Id *remUses = new Id[numNodes];
	bool *dirty = new bool[numNodes];
	set<Id> redPblNodes;
	unsigned long cost = 0;

	Id *loc = new Id[numNodes];
	for(Id i=0; i<numNodes; ++i)
	{
		remUses[i] = cdag->getNode(i)->succsList.size();
		loc[finalSchedule[i]] = i;
		if(cdag->getNode(i)->predsList.size() <= 0)
			dirty[i] = false;
		else
			dirty[i] = true;
	}

	int remPbls = K;
	set<Id> predSet;
	for(Id i=0; i<numNodes; ++i)
	{
		assert(remPbls <= K);
		assert(redPblNodes.size() == (K-remPbls));
		/*
		if(remPbls < 3)
		{
			cout << "Spill: " << i << '\n';
			cost += K;
			redPblNodes.clear();
			remPbls = K;
		}
		*/
		
		Id nodeId = finalSchedule[i];
		Id numPreds = cdag->getNode(nodeId)->predsList.size();
		// Check for input nodes
		if(numPreds <= 0)
		{
			continue;
		}
		
		for(Id j=0; j<numPreds; ++j)
		{
			predSet.insert(cdag->getNode(nodeId)->predsList[j]);
		}
		for(Id j=0; j<numPreds; ++j)
		{
			Id pred = cdag->getNode(nodeId)->predsList[j];
			if(redPblNodes.find(pred) != redPblNodes.end())
				continue;
			//spill
			if(remPbls == 0)
			{
				spill(cdag, numNodes, predSet, redPblNodes, loc, dirty, remPbls, cost, i);
			}
			//Place red pebble
			redPblNodes.insert(pred);
			// cout << "Insert1 " << pred << "\n";
			++cost;
			--remPbls;
		}
		//Throw red pebble from a node once all its uses are done
		for(Id j=0; j<numPreds; ++j)
		{
			Id pred = cdag->getNode(nodeId)->predsList[j];
			Id rem = --remUses[pred];
			if(rem == 0)
			{
				++remPbls;
				redPblNodes.erase(pred);
				// cout << "Removed " << pred << "\n";
				// cout.flush();
			}
		}
		//place red pebble for firing node
		predSet.clear();
		if(remPbls == 0)
		{
			spill(cdag, numNodes, predSet, redPblNodes, loc, dirty, remPbls, cost, i);
		}
		// is output node
		if(cdag->getNode(nodeId)->succsList.size() <= 0) //in case of output node, move the node from red to blue
		{
			++cost;
			dirty[nodeId] = false;
		}
		else
		{
			redPblNodes.insert(nodeId);
			// cout << "Insert2 " << nodeId << "\n";
			--remPbls;
		}
	}

	delete []loc;
	delete []remUses;
	delete []dirty;
	return cost;
}
}

#endif
