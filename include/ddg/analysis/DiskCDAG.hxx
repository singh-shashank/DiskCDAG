#ifndef DiskCDAG_HXX
#define DiskCDAG_HXX


//#define FULL_DAG //If defined, whole ddg is built, else just the CDAG is built

#include "ddg/analysis/LazyGraph.hxx"
#include "CDAGCounter.hxx"

#include <boost/type_traits.hpp>
#include <map>
#include <set>
#include <climits>
#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <typeinfo>
#include <sstream>
#include <istream>
#include <queue>
#include <deque>
#include <boost/config.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "ddg/analysis/DiskGraphNode.hxx"
#include "ddg/analysis/DiskCache.hxx"

namespace ddg{
	typedef unsigned char BYTE;
}

namespace utils{

using namespace std;
using namespace ddg;

// Method for calculating storage space needed to represent
// each node by a bit.
inline int convertNumNodesToBytes(const Id &numNodes)
{
	// You can represent 8 nodes with a byte
	if(numNodes % 8 == 0)
		return int(numNodes >> 3);
	else
		return int(numNodes >> 3) + 1;
}

void dumpBitSet(ddg::BYTE *bitSet, int numSets)
{
	cout << "\n";
	for(int i=0;i<numSets; ++i)
	{
		cout << (int)bitSet[i] << " ";
	}
	cout << "\n";
	cout << flush;
}

void setBitInBitset(BYTE *bitSet, int bitNum, int numBitSets)
{
	int groupIndex = bitNum >> 3;  // divide by 8
	assert(groupIndex < numBitSets);
	int bitIndex = bitNum % 8;
	BYTE byteMask = 1 << (7 - bitIndex);
	bitSet[groupIndex] = bitSet[groupIndex] | byteMask;
}

bool isBitSet(BYTE *bitSet, int bitNum, int numBitSets)
{
	int groupIndex = bitNum >> 3;  // divide by 8
	assert(groupIndex < numBitSets); 
	int bitIndex = bitNum % 8;
	BYTE byteMask = 1 << (7 - bitIndex);
	return bitSet[groupIndex] & byteMask;
}

void unsetBitInBitset(BYTE *bitSet, int bitNum, int numBitSets)
{	
	int groupIndex = bitNum >> 3;  // divide by 8
	assert(groupIndex < numBitSets);
	int bitIndex = bitNum % 8;
	BYTE byteMask = 0;
	byteMask = ~(byteMask) ^ (1 << (7 - bitIndex));
	bitSet[groupIndex] = bitSet[groupIndex] & byteMask;
}

void getOnesPositionsInBitSet(BYTE *bitSet, int numBitSets, std::vector<Id> &setPos)
{
	BYTE zeroMask = 0;

	for(unsigned int i=0; i<numBitSets; ++i)
	{
		if(bitSet[i] | zeroMask != 0)
		{
			// we have some 1's in this byte
			for(unsigned int j=(1<<7), pos=0; j>0; j=j>>1, ++pos)
			{
				if(bitSet[i] & j)
				{
					setPos.push_back(i*8 + pos);
				}
			}
		}
	}
}
}

namespace ddg
{
using namespace llvm;
using namespace std;

#ifdef FULL_DAG
typedef size_t payload_type;
#else
typedef std::set<size_t> payload_type;
#endif

template <typename Node>
class DiskCDAGBuilder;

template <typename CDAGNode>
class DiskCDAG;

// Temporary files used for graph generation
const string tempSuccsCountFNSuffix = "succscounttemp";
const string tempGraphDumpFNSuffix = "graphdump";
const string tempSuccsListFNSuffix = "succslisttemp";
const string configFileName = "diskgraphconfiguration.txt";
const string tempNodeAddressFNSuffix = "nodeaddress";
const string tempNeighborFNSuffix = "neighborinfo";

// Final dump of graph
string diskGraphFNSuffix = "diskgraph";
string diskGraphIndexFNSuffix = "diskgraph_index";
unsigned int NUM_SLOTS = 1024;
unsigned int BLOCK_SIZE = 4;
bool printGraphInAsciiFormat = false;
bool cleanUpTemporaryFiles = true;
bool graphCreatedFlag = false;
bool printGraphInDotFormat = false;

// Other global DS
deque<Id> curIV;

template <typename Node>
class DiskCDAGBuilder: public LazyGraphVisitor<payload_type>
{
	protected:
		DiskCDAG<Node> *cdag;
		map<Address, size_t> loadMap; //Maps the memory address to ddg input node id
		bool getCountsFlag;
		Id numNodes;
		string bcFileName;
		fstream successorCountFile;
		fstream nodeAddrFile;

	public:
		DiskCDAGBuilder(DiskCDAG<Node> *cdag) : cdag(cdag), 
											loadMap(), 
											getCountsFlag(false), 
											numNodes(0),
											successorCountFile(""),
											nodeAddrFile("")
		{

		}

		DiskCDAGBuilder() : cdag(0), loadMap(), getCountsFlag(true), 
							numNodes(0), successorCountFile(""),
							nodeAddrFile("")
		{

		}

		DiskCDAGBuilder(string fileName) : cdag(0), loadMap(), 
											getCountsFlag(true), 
											numNodes(0), 
											bcFileName(fileName)
		{
			successorCountFile.open((bcFileName+tempSuccsCountFNSuffix).c_str(), std::fstream::out | std::fstream::trunc);
			successorCountFile.close();
			successorCountFile.open((bcFileName+tempSuccsCountFNSuffix).c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);

			nodeAddrFile.open((bcFileName+tempNodeAddressFNSuffix).c_str(), std::fstream::out | std::fstream::trunc);
			nodeAddrFile.close();
			nodeAddrFile.open((bcFileName+tempNodeAddressFNSuffix).c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
		}

		Id getNumNodes(){return numNodes;}

		virtual void visitNode(LazyNode<payload_type> *node, Predecessors	&predecessors, Operands &operands);


		void incSuccessorCountInFile(Id nodeId);
		size_t incNumNodesCounter(Address addr);
		void printSuccessorCountFile();
		void updateNodeAddress(Id nodeId, Address addr);
		void printAddressFile();

};

class DiskCDAGBuilderWithIV : public DiskCDAGBuilder<GraphNodeWithIV>
{
protected:
	unsigned int curDepth;
public:
	DiskCDAGBuilderWithIV(DiskCDAG<GraphNodeWithIV> *cdag) : 
								DiskCDAGBuilder(cdag)
	{
		curDepth = 0;
		curIV.clear();
	}

	DiskCDAGBuilderWithIV() : DiskCDAGBuilder()
	{
		curDepth = 0;
		curIV.clear();
	}

	DiskCDAGBuilderWithIV(string fileName): DiskCDAGBuilder(fileName)
	{
		curDepth = 0;
		curIV.clear();
	}
	virtual void visitLoopBegin(Id loopId);
	virtual void visitLoopEnd(Id loopId);
	virtual void visitLoopIterBegin();
	virtual void visitLoopIterEnd();
	virtual void visitNode(LazyNode<payload_type> *node, Predecessors	&predecessors, Operands &operands);
};

struct DataList
{
	Id id;
	std::vector<Id> list;
	Id listCapacity;
	DataList(): id(0), listCapacity(0)
	{

	}

	~DataList()
	{
		list.clear();
	}

	Id getId(){return id;}

	void writeToStream(fstream &file)
	{
		Id temp =0;
		file.write((const char*)&id, sizeof(Id));
		file.write((const char*)&listCapacity, sizeof(Id));
		file.write((const char*)&list[0], sizeof(Id)*list.size());
		for(Id i=0; i<(listCapacity-list.size()); ++i)
		{
			file.write((const char*)&temp, sizeof(Id));
		}
	}

	bool readNodeFromASCIIFile(istream &file)
	{
		listCapacity = 0;
		file.read((char*)&id, sizeof(Id));
		file.read((char*)&listCapacity, sizeof(Id));		
		for(Id i=0; i<listCapacity;++i)
		{
			Id temp =0;
			file.read((char*)&temp, sizeof(Id));
			// TODO : comparing with 0 is a hack for successor list here
			// because '0' in dynamic graph cannot be successor of any node
			// thus this check works. 
			if(temp > 0)
			{
				list.push_back(temp);
			}
		}
	}

	bool readNodeFromBinaryFile(istream &file)
	{
		listCapacity = 0;
		file.read((char*)&id, sizeof(Id));
		file.read((char*)&listCapacity, sizeof(Id));		
		for(Id i=0; i<listCapacity;++i)
		{
			Id temp =0;
			file.read((char*)&temp, sizeof(Id));
			if(temp > 0)
			{
				list.push_back(temp);
			}
		}
		return false; // TODO : Check for errors and return
	}

	void print(ostream &out) const
	{
		out << "\n Id : " << id;
		out << " ListCapacity : " << listCapacity;
		out << " List (size = " << list.size() <<") : ";
		for(vector<Id>::const_iterator it = list.begin();
			it != list.end(); ++it)
		{
			out << *it << " ";
		}
		out << flush;
	}

	void addToList(Id item)
	{
		if(list.size() == listCapacity)
		{
			cout << "\nError : in addToList() of " << id;
			cout << " trying to add item : "<< item <<" in already full list\n";
			return;
		}
		list.push_back(item);
	}

	void reset()
	{
		id =0, listCapacity = 0, list.clear();
	}

	size_t getSize(unsigned int bs = 0)
	{
		// This is just a rough estimate - more accurately a lower bound.
		// But this wouldn't deviate a lot from actual size occupied
		// in memory.
		/*size_t retVal = 0;
		retVal += sizeof(Id) + sizeof(Id);
		retVal += (list.size())*sizeof(Id);
		retVal = retVal > sizeof(*this) ? retVal : sizeof(*this);
		if(bs != 0 && retVal >= bs)
		{
			cout << "\nError : Size of datalist node with id : " << id;
			cout << " is " << retVal << ", which is bigger then ";
			cout << " the specified block size of " << bs;
			exit(0);
		}
		return retVal;*/

		/* 	TODO: Fix this
			The above commented code seems to not work with this
			data structure, especially because its being used for
			writing back to the file after changes in case of 
			successor info.
		*/
		return sizeof(*this);
	}

};

template <typename CDAGNode>
class DiskCDAG
{
	public:
		unsigned int maxDepth;

	private:

		static const size_t CDAGNODE_SIZE = sizeof(CDAGNode);

		BYTE *succsBitSet;
		BYTE *nodeMarkerBitSet;
		size_t numOfBytesForSuccBS;
		size_t numOfBytesFornodeMarkerBitSet;

		size_t blockCount;
		size_t curBlockSize;

		typename std::map<Id, CDAGNode*> idToCDAGNodeMap;

		fstream graphDumpFile;
		fstream succsListTempFile;
		fstream succsListNewTempFile;
		ifstream nodeAddrFile;

		// TODO : Temp ofstreams - remove these!
		ofstream printBeforeWriteFile;
		ofstream printAfterReadFile;

		size_t numNodes; //No. of nodes created so far
		size_t count; //Total expected no. of nodes

		// FileNames
		const string bcFileName;
		string diskGraphFileName;
		string diskGraphIndexFileName;

		DiskCache<DataList, Id> *succsCache;
		DiskCache<NodeNeighborInfo, Id> *neighborCache;


		queue<CDAGNode*> availableCDAGNodesQ;

		ofstream graphInAscii;




		void init(size_t count)
		{
			lruCache = 0;
			succsCache = 0;
			neighborCache = 0;
			maxDepth = 0;
			numOfBytesForSuccBS = utils::convertNumNodesToBytes(count);
			succsBitSet = new BYTE[numOfBytesForSuccBS];
			nodeMarkerBitSet = 0; // to be initialized if we get actual node count
			memset(succsBitSet, 0, numOfBytesForSuccBS);

			// TODO : compare with CDAGNODE_WITHSUCC_SIZE
			if(BLOCK_SIZE != 0 && BLOCK_SIZE < CDAGNODE_SIZE)
				cout << "\n Block size is less than CDAG node size..Aborting!";
			else if(BLOCK_SIZE == 0){
				cout << "\n Block size is passed as zero - memory based graph will be generated.";
			}

			if(!graphCreatedFlag)
			{
				diskGraphFileName = (bcFileName+diskGraphFNSuffix);
				diskGraphIndexFileName = (bcFileName+diskGraphIndexFNSuffix);
				initForSecondPass();
			}
			else
			{
				if(diskGraphFNSuffix.empty())
				{
					cout << "\n Error empty disk graph filename passed.";
					exit(1); // TODO : set error flag instead of exiting
				}
				// graph is already created and the filename is passed along
				diskGraphFileName = (diskGraphFNSuffix);
				initLRUCacheForDiskGraph();
				count = numNodes = lruCache->getDataCount()+1;
				cout << "\n Read graph from the file '" << diskGraphFileName;
				cout << " ' with node count as : " << numNodes;
			}


		}

		void initForSecondPass()
		{
			//  TODO : revisit this part for checking corner cases and add some comments
			graphDumpFile.open((bcFileName+tempGraphDumpFNSuffix).c_str(), std::fstream::out | std::fstream::trunc);
			graphDumpFile.close();
			graphDumpFile.open((bcFileName+tempGraphDumpFNSuffix).c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
			
			succsListTempFile.open((bcFileName+tempSuccsListFNSuffix).c_str(), std::fstream::out | std::fstream::trunc);
			succsListTempFile.close();
			succsListTempFile.open((bcFileName+tempSuccsListFNSuffix).c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);

			string nodeAddrTempFileName = bcFileName + tempNodeAddressFNSuffix;			
			nodeAddrFile.open(nodeAddrTempFileName.c_str());

			if(printGraphInAsciiFormat)
			{
				graphInAscii.open("graphInAscii");
			}

			// initialize the new successor list file
			initSuccessorListFile();

			//succsCache = new DiskCache<DataList, Id>(BLOCK_SIZE, NUM_SLOTS, true);
			succsCache = new DiskCache<DataList, Id>(2*1024, 512, true);
			if(!succsCache->init(bcFileName+tempSuccsListFNSuffix))
			{
				cout << "\n Cache initialization for successor's list failed...";
				return;
			}
		}

		void initSuccessorListFile()
		{
			fstream succsCountTempFile;
			string succsCountTempFileName = bcFileName+tempSuccsCountFNSuffix;
			succsCountTempFile.open(succsCountTempFileName.c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
			succsCountTempFile.seekg(0, ios::beg);
			Id succCount =0;
			Id id = 0;
			Id temp = 0;
			while(!succsCountTempFile.read((char*)&succCount, sizeof(Id)).eof())
			{
				succsListTempFile.write((const char*)&id, sizeof(Id));
				succsListTempFile.write((const char*)&succCount, sizeof(Id));
				for(Id i=0; i<succCount; ++i)
				{
					succsListTempFile.write((const char*)&temp, sizeof(Id));
				}
				++id;
			}
			succsListTempFile.close();

			// Delete the temp count file
			if(cleanUpTemporaryFiles)
			{
				remove(succsCountTempFileName.c_str());
			}
		}

		void initLRUCacheForDiskGraph()
		{
			if(!lruCache)
			{
				cout << "\n Initialize LRU cache";
	      		lruCache = new DiskCache<CDAGNode, Id>(BLOCK_SIZE, NUM_SLOTS);
				//if(!lruCache->init(diskGraphFileName, diskGraphIndexFileName ))
				if(!lruCache->init(diskGraphFileName))
				{
					cout <<"\n Cache initialization failed..stopping execution";
					return;
				}
				cout << "\n LRU Disk Cache initialized.";
			}
		}

	public:
		DiskCDAG(size_t count) : numNodes(0),
								 count(count),
								 blockCount(0),
								 curBlockSize(0)
		{
			init(count);
		}

		DiskCDAG(Ids &ids, const string &bcFile, Id nodeCount):
										numNodes(0),
									 	blockCount(0),
									 	curBlockSize(0),
									 	bcFileName(bcFile),
									 	count(nodeCount)
		{
			//CDAGCounter counter(ids); //Get the count of expected no. of nodes
			//count = counter.getCount();
			init(count);
		}

		DiskCDAG(Ids &ids, const string &bcFile) : numNodes(0),
									 	blockCount(0),
									 	curBlockSize(0),
									 	bcFileName(bcFile),
									 	count(0)
		{
			//CDAGCounter counter(ids); //Get the count of expected no. of nodes
			//count = counter.getCount();
			init(count);
		}

		~DiskCDAG()
		{

			delete []succsBitSet;

			if(nodeMarkerBitSet)
			{
				delete []nodeMarkerBitSet;
			}

			// delete the graph nodes and clean up the maps
			typename std::map<Id, CDAGNode*>::iterator it1 = idToCDAGNodeMap.begin();
			for(; it1 != idToCDAGNodeMap.end(); ++it1)
			{
				delete it1->second;
				it1->second = 0;
			}
			idToCDAGNodeMap.clear();

			if(lruCache)
			{
				delete lruCache;
				lruCache = 0;
			}
		}

		CDAGNode* getAvailableCDAGNode()
		{
			CDAGNode *retVal = 0;
			if(availableCDAGNodesQ.empty())
			{
				retVal = new CDAGNode();
				availableCDAGNodesQ.push(retVal);
			}			
			retVal = availableCDAGNodesQ.front();
			availableCDAGNodesQ.pop();
			retVal->reset();

			return retVal;
		}

		//Returns the no. of nodes in the cdag
		size_t getNumNodes()
		{
			return numNodes;
		}

		size_t getNumOfBytesForNodeMarkerBS()
		{
			return numOfBytesFornodeMarkerBitSet;
		}

		void readAddressFromFile(Id &nodeId, Address &addr)
		{
			streampos pos(sizeof(Address)*nodeId);
			nodeAddrFile.seekg(pos, fstream::beg);
			nodeAddrFile.read((char*)&addr, sizeof(Address));
		}

		

		void setPredecessor(size_t nodeId, const set<size_t> &predSet)
		{
			assert(nodeId < numNodes);
			size_t size = predSet.size();
			set<size_t>::const_iterator it = predSet.begin();
			for(size_t i=0; i<size; ++i)
			{
				idToCDAGNodeMap[nodeId]->predsList.push_back(*it);
				DataList *l = succsCache->getData((*it));
				l->addToList(nodeId);
				++it;
			}

			addUpdateNodeToBlock(idToCDAGNodeMap[nodeId]);
		}
		
		// Mark a node
		void markNode(BYTE *bitSet, Id nodeId)
		{
			if(!bitSet)
			{
				numOfBytesFornodeMarkerBitSet = utils::convertNumNodesToBytes(numNodes);
				bitSet = new BYTE[numOfBytesFornodeMarkerBitSet];
				memset(bitSet, 0, numOfBytesFornodeMarkerBitSet); // unmark all the node by default
			}
			utils::setBitInBitset(bitSet, nodeId, numOfBytesFornodeMarkerBitSet);
		}

		// Unmark a node
		void unmarkNode(BYTE* bitSet, Id nodeId)
		{
			if(!bitSet)
			{
				numOfBytesFornodeMarkerBitSet = utils::convertNumNodesToBytes(numNodes);
				bitSet = new BYTE[numOfBytesFornodeMarkerBitSet];
				memset(bitSet, 0, numOfBytesFornodeMarkerBitSet); // unmark all the node by default
				return;
			}
			utils::unsetBitInBitset(bitSet, nodeId, numOfBytesFornodeMarkerBitSet);
		}

		// Unmark all the nodes
		void unmarkAllNodes()
		{
			if(!nodeMarkerBitSet)
			{
				numOfBytesFornodeMarkerBitSet = utils::convertNumNodesToBytes(numNodes);
				nodeMarkerBitSet = new BYTE[numOfBytesFornodeMarkerBitSet];
			}
			memset(nodeMarkerBitSet, 0, numOfBytesFornodeMarkerBitSet);
		}

		// Is node marked/unmarked
		bool isNodeMarked(BYTE *bitSet, const Id &nodeId)
		{
			return utils::isBitSet(bitSet, nodeId, numOfBytesFornodeMarkerBitSet);
		}

		// Finds the first unmarked node (i.e. the first 0 in bitset)
		// If there is a node it returns true
		// Otherwise false. 
		bool getFirstReadyNode(BYTE *bitSet, Id &nodeId)
		{
			bool retVal = false;
			BYTE oneMask = ~0;
			for(unsigned int i=0; i<numOfBytesFornodeMarkerBitSet; ++i)
			{
				if(bitSet[i] ^ oneMask != 0)
				{
					// we have zero in here somewhere
					for(unsigned int j=(1<<7), pos=0; j>0; j=j>>1, ++pos)
					{
						if((bitSet[i] & j) == 0)
						{
							nodeId = i*8 + pos;
							retVal = true;
							break;
						}
					}
				}
				if(retVal)
					break; // node found so break;
			}
			if(nodeId >= numNodes) // is there a better way to handle this?
				retVal = false;
			return retVal;
		}

		// Finds the first unmarked node (i.e. the first 0 in bitset)
		// If there is a node it returns true
		// Otherwise false. 
		// Overloaded method for optimization. Take in the
		// last accessed index to start from there
		bool getFirstReadyNode(BYTE *bitSet, Id &nodeId, unsigned int &lastIndex)
		{
			bool retVal = false;
			BYTE oneMask = ~0;
			unsigned int i=lastIndex;
			for(; i<numOfBytesFornodeMarkerBitSet; ++i)
			{
				if(bitSet[i] ^ oneMask != 0)
				{
					// we have zero in here somewhere
					for(unsigned int j=(1<<7), pos=0; j>0; j=j>>1, ++pos)
					{
						if((bitSet[i] & j) == 0)
						{
							nodeId = i*8 + pos;
							retVal = true;
							break;
						}
					}
				}
				if(retVal)
					break; // node found so break;
			}
			if(nodeId >= numNodes) // is there a better way to handle this?
				retVal = false;
			else
				lastIndex = i;
			return retVal;
		}

		

		void cleanUpTemporaries()
		{
			// Cleaning up the temporary disk files and data structures
			cout <<"\n Cleaning up the temporary files and data structures now...";
			if(graphDumpFile)
			{
				graphDumpFile.close();
			}
			
			resetGraphState(true);


			if(succsCache)
			{
				delete succsCache;
				succsCache = 0;
			}
			if(cleanUpTemporaryFiles)
			{
				remove((bcFileName+tempGraphDumpFNSuffix).c_str());
				remove((bcFileName+tempSuccsListFNSuffix).c_str());
				remove((bcFileName+tempNodeAddressFNSuffix).c_str());
			}

			// we can also clear up the memory pool here
			// clear up all the Data nodes
			while(!availableCDAGNodesQ.empty())
			{
				CDAGNode* head = availableCDAGNodesQ.front();
				availableCDAGNodesQ.pop();
				delete head;
				head = 0;
			}
			cout << "done";
		}

		void updateGraphWithSuccessorInfo()
		{
			ofstream diskGraph(diskGraphFileName.c_str(), ios::binary);
			ofstream diskGraphIndex(diskGraphIndexFileName.c_str(), ios::binary);

			streampos pos(0);
			size_t curBlockSize = 0;
			size_t bs = BLOCK_SIZE*NUM_SLOTS;

			graphDumpFile.clear();
	 		graphDumpFile.seekg(0, ios::beg);
	 		blockCount = 0;

	 		//cin.get();

			while(!readBlockFromFile(graphDumpFile))
			{
				writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex, 
					BLOCK_SIZE, pos, curBlockSize);
				++blockCount;
			}

			writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex,
			 BLOCK_SIZE, pos, curBlockSize);
			diskGraph.close();
			diskGraphIndex.close();

			cleanUpTemporaries();
		}

		void writeGraphWithSuccessorInfoToFile(ofstream &diskGraph,
											   ofstream &diskGraphIndex,
											   size_t bs,
											   streampos &pos,
											   size_t &curBlockSize)
		{
			
			vector<CDAGNode*> nodesToWriteList;
			vector<Id> succList;
			
			typename map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
			for(; it != idToCDAGNodeMap.end(); ++it)
			{
				//readSuccessorsFromFile(it->first, succList);
				succList.clear(); // optimization : use a pointer to the vector
				DataList * l = succsCache->getData(it->first);
				CDAGNode *curNode = it->second;
				if(l->list.size() > 0)
				{
					//curNode->succsList = l->list;
					for(vector<Id>::iterator it1 = l->list.begin();
						it1 != l->list.end(); ++it1)
					{
						curNode->succsList.push_back(*it1);
					}
				}

				// Check if we reached the write block size
				size_t curNodeSize = curNode->getSize(BLOCK_SIZE);//sizeof(*curNode);
				if(curBlockSize + curNodeSize > bs)
				{
					// dump out nodesToWriteList
					dumpOutCurrentNodeListToDiskGraphFile(diskGraph,
														diskGraphIndex,
														nodesToWriteList, 
														pos);
					
					// reset the variables
					nodesToWriteList.clear();
					curBlockSize = 0;
				}	
				curBlockSize += curNodeSize;
				nodesToWriteList.push_back(curNode);					
			}

			// dump out any remaining nodes
			if(nodesToWriteList.size() > 0)
			{
				dumpOutCurrentNodeListToDiskGraphFile(diskGraph,
														diskGraphIndex,
														nodesToWriteList, 
														pos);
			}
		}

		void dumpOutCurrentNodeListToDiskGraphFile(	ofstream &diskGraph,
											   		ofstream &diskGraphIndex,
											   		vector<CDAGNode*> &nodesToWriteList,
													streampos &pos)
		{
			stringstream ss;
			typename vector<CDAGNode*>::iterator it1 = nodesToWriteList.begin();
			int prev = -1;
			for(; it1 != nodesToWriteList.end(); ++it1)
			{
				// (*it1)->writeToStream(ss);
				// diskGraph << ss.str();
				(*it1)->writeToStreamInBinary(diskGraph);
				// Enable this to get a human-readble graph output
				if(printGraphInAsciiFormat)
				{
					(*it1)->print(graphInAscii);
				}

				Id curId = (*it1)->getId() + 1;
	 			int perc = (curId*100)/count;
	 			if(perc%5 == 0 && prev != perc)
	 			{
	 				prev = perc;
	 				cout << "\r\033[K ";
	 				cout << "Processed node with id :" << curId;
	 				cout << " ( of " << count << "nodes) - ";
	 				cout << perc << "% done." << flush;
	 			}
			//diskGraphIndex.write((const char*)&pos, sizeof(streampos));
			}
			pos = diskGraph.tellp();
		}

		bool initNeighborCache()
		{
			if(!lruCache)
			{
				cout << "\nThe disk graph is not yet created..too early to write neighbor information..exiting!";
				return false;
			}
			cout << "\n Writing neighbor information to a file..." << flush;
			ofstream out;
			out.open(string(bcFileName+tempNeighborFNSuffix).c_str(), std::fstream::out | fstream::trunc);
			out.close();
			out.open(string(bcFileName+tempNeighborFNSuffix).c_str(), std::fstream::out | fstream::binary);
			set<Id> nSet;
			NodeNeighborInfo *tempNeighborInfo = new NodeNeighborInfo(); 
			CDAGNode node;
			for(Id i=0; i<numNodes; ++i)
			{
				nSet.clear();
				node.reset();
				node.deepCopy(lruCache->getData(i));
				Id succCount = node.succsList.size();
				for(Id j=0; j<succCount; ++j)
				{
					CDAGNode *succNode = lruCache->getData(node.succsList[j]);
					nSet.insert(succNode->predsList.begin(),succNode->predsList.end());
				}
				// remove the current node
				nSet.erase(node.dynId);

				// Write neighbor set out to a file
				tempNeighborInfo->reset();
				tempNeighborInfo->dynId = node.dynId;				
				for(set<Id>::iterator it = nSet.begin(); it != nSet.end();
					++it)
				{
					tempNeighborInfo->neighborList.push_back(*it);
				}
				tempNeighborInfo->writeToStreamInBinary(out);
				
			}
			out.close();
			delete tempNeighborInfo;
			cout << "Done!" << flush;

			cout << "\n Initializing neighbor cache...";
			neighborCache = new DiskCache<NodeNeighborInfo, Id>(BLOCK_SIZE, NUM_SLOTS);
			if(!neighborCache->init(bcFileName+tempNeighborFNSuffix))
			{
				cout << "\n Cache initialization for neighbor information failed..";
				return false;
			}
			cout << "Done!" << flush;
			return true;

		}

		static void readConfigurationFromFile()
		{
			/*
				Expected format of the configuration file:
				
				1024 # number of slots in cache #0
				4 # block size in kilobytes #1
				0 # '1' implies a graph with filename supplied in next line is already created. Graph will not be created again #2
				diskgraph # suffix for disk graph file (bc file name will be appended) or actual disk graph file#3
				0 # set to 1 if you want to write graph info in ascii file #4
				1 # set to 0 if you don't want temp files to be cleaned up after use #5
				diskgraph_index # suffix for disk graph index file (bc file name will be appended) #6
			*/
			namespace po = boost::program_options;
			fstream configFile(configFileName.c_str(), ios::in);

			po::options_description options("Options");
				options.add_options()
					("DiskGraph.NUM_SLOTS", 
					po::value(&NUM_SLOTS)->default_value(1024), 
					"Number of slots");
				options.add_options()
					("DiskGraph.BLOCK_SIZE",
					po::value(&BLOCK_SIZE)->default_value(4),
					"Block size in KB");
				options.add_options()
					("DiskGraph.CREATE_GRAPH",
					po::value(&graphCreatedFlag)->default_value(false),
					"Use an alrady created graph file (will use the filename passed in for disk graph)");
				options.add_options()
					("DiskGraph.DISK_GRAPH_FN",
					po::value(&diskGraphFNSuffix)->default_value("diskgraph"),
					"Suffix for the disk graph file or the actual file if the CREATE_GRAPH option is set to true");
				options.add_options()
					("DiskGraph.PRINT_GRAPH_ASCII",
					po::value(&printGraphInAsciiFormat)->default_value(false),
					"Prints the graph in ascii format to a file named 'asciiGraphPrint'");
				options.add_options()
					("DiskGraph.PRINT_GRAPH_DOT",
					po::value(&printGraphInDotFormat)->default_value(false),
					"Prints the graph in dot format to a file named 'diskgraph.dot'");
				options.add_options()
					("DiskGraph.CLEAN_UP_TEMP_FILES",
					po::value(&cleanUpTemporaryFiles)->default_value(true),
					"Clean up temporary files created");
			bool err = false;
			if(configFile.is_open())
			{
				try
				{
					po::variables_map vm;
					po::store(po::parse_config_file(configFile, options), vm);
					po::notify(vm);
					cout << "\nRead configuration file : ";
					cout << "\nNUM_SLOTS = " << NUM_SLOTS;
					cout << "\nBLOCK_SIZE = " << BLOCK_SIZE << " KB";
					cout << "\n";
				}
				catch(...)
				{
					remove(configFileName.c_str());
					err = true;
				}
			}
			else
			{
				err = true;
			}
			if(err)
			{
				cout << "\nWarning: Configuaration file for Disk Graph not found or file is corrupted!";
				cout << "\nUsing default values and generating a default config file as well.";
				configFile.open(configFileName.c_str(), ios::out);

				configFile << "[DiskGraph]";
				configFile << "\nNUM_SLOTS = " << NUM_SLOTS << "\t #" <<options.options()[0]->description();
				configFile << "\nBLOCK_SIZE = " << BLOCK_SIZE << "\t #" <<options.options()[1]->description();;
				configFile << "\nCREATE_GRAPH = " << graphCreatedFlag << "\t #" <<options.options()[2]->description();;
				configFile << "\nDISK_GRAPH_FN = " << diskGraphFNSuffix << "\t #" <<options.options()[3]->description();;
				configFile << "\nPRINT_GRAPH_ASCII = " << printGraphInAsciiFormat << "\t #" <<options.options()[4]->description();;
				configFile << "\nPRINT_GRAPH_DOT = " << printGraphInDotFormat << "\t #" <<options.options()[5]->description();;
				configFile << "\nCLEAN_UP_TEMP_FILES = " << cleanUpTemporaryFiles << "\t #" <<options.options()[6]->description();
			}
			configFile.close();
		}

		//Generates the DiskCDAG using DiskCDAGBuilder
		template<typename Node, typename GraphBuilder>
		static DiskCDAG<Node>* generateGraph(Ids& ids, const string &bcFileName)
		{
			DiskCDAG<Node> *cdag;
			readConfigurationFromFile();
			BLOCK_SIZE = BLOCK_SIZE * 1024; // Convert to BYTES

			if(!graphCreatedFlag)
			{
				cout <<"\n-----Starting First pass over the trace to get counts." << flush;
				GraphBuilder countBuilder(bcFileName);
				countBuilder.visit(ids);
				cout <<"\n-----Pass complete. Number of nodes : "<<countBuilder.getNumNodes() << flush;
				//countBuilder.printSuccessorCountFile();
				//countBuilder.printAddressFile();
				//cin.get();
				cdag = new DiskCDAG<Node>(ids, bcFileName, 
					countBuilder.getNumNodes());

				cout << "\n \n ";
				cout <<"\n-----Starting Second Pass over the trace to dump out the graph.\n";
				cout << "\n Step 1 (of 2) : Writing temporary graphdump and successor information now!\n"<<flush;

				GraphBuilder builder(cdag);
				builder.visit(ids);
				cdag->flushCurrentBlockToDisk(true);
				cout << "\n Done!" <<flush;

				cout << "\n\n";

				cout <<"\n Step 2 (of 2) : Updating graph with successor information \n" << flush;
	      		cdag->updateGraphWithSuccessorInfo();      		
	      		cout << "\n Done!" <<flush;
	      		cout <<"\n-----Pass complete. Graph written to '*diskgraph' file.";
	      		cout << "\n";
      		}
      		else
      		{
      			// TODO : read graph with IV info as well
      			cout << "\nRead graph from file configuration specified.\n";
      			cdag = new DiskCDAG<Node>(ids, bcFileName);
      		}

      		cdag->initLRUCacheForDiskGraph();

      		if(printGraphInDotFormat)
      		{
      			cdag->printDOTGraph("diskgraph.dot");
      		}

			return cdag;
		}

		//Generates the DiskCDAG using user-specified 'Builder'
		template <typename Builder>
			static DiskCDAG<GraphNode>* generateGraph(Ids& ids, string &bcFileName)
			{
				cout <<"\n First pass through trace to get counts" << flush;
				DiskCDAGBuilder<GraphNode> countBuilder(bcFileName);
				countBuilder.visit(ids);
				cout <<"\n Pass complete. Number of nodes : "<<countBuilder.getNumNodes() << flush;
				//countBuilder.printSuccessorCountFile();

				DiskCDAG<GraphNode> *cdag = new DiskCDAG<GraphNode>(ids, bcFileName, 
				countBuilder.getNumNodes());

				cout << "\n Writing graphdump now!"<<flush;
				Builder builder(cdag);

				builder.visit(ids);
				cout << "\n Done writing graphdump now!" <<flush;

				return cdag;
			}

		void performTraversal(string outFile, bool dfsFlag)
		{
			cout << "\n Starting traversal on graph with " << numNodes << " nodes.";
			if(dfsFlag)
			{
				cout << "\n Traversal type: Depth First Search";
			}
			else
			{
				cout << "\n Traversal type: Breadth First Search";	
			}
			cout << "\n";

			if(!lruCache)
			{
				cout << "\n Error : Cache not initialized for the graph..exiting";
				return;
			}
			
			Id processedNodeCount = 0;
			unmarkAllNodes(); // mark all nodes as ready
			deque<Id> q;
			Id startV;
			//vector<Id> bfsOutput;
			//ofstream bfsOutFile(outFile.c_str());
			bool error = false;
			unsigned int bitSetIndex = 0;
			int prev = -1;
			while(getFirstReadyNode(nodeMarkerBitSet, startV, bitSetIndex))
			{
				//bfsOutFile << "\nStarting vertex : " << startV << "\n";
				q.push_back(startV);
				markNode(nodeMarkerBitSet, startV);
				
				while(!q.empty())
				{
					const CDAGNode *curNode = 0;
					if(dfsFlag)
					{
						curNode  = lruCache->getData(q.back());
						q.pop_back();
					}
					else
					{
						curNode  = lruCache->getData(q.front());
						q.pop_front();
					}
					if(!curNode)
					{
						cout <<"\n Failed to get " << q.front() << " node..stopping traversal";
						error = true;
						break;
					}
					//bfsOutput.push_back(curNode->dynId);
					//bfsOutFile << curNode->dynId << " ";
					for(std::deque<Id>::const_iterator it = curNode->succsList.begin();
						it != curNode->succsList.end(); ++it)
					{
						if(!isNodeMarked(nodeMarkerBitSet, *it))
						{
							q.push_back(*it);
							markNode(nodeMarkerBitSet, *it);							
						}
						else
						{
							// its already visited
						}
					}
					++processedNodeCount;
					int perc = (processedNodeCount*100)/numNodes;
					if(perc % 1 == 0 && perc != prev)
					{
						cout << "\r\033[K ";
						cout << "Number of nodes processed : " << processedNodeCount;
						cout << " (of " << numNodes << ") - ";
						cout << perc << " % done.";
						cout << flush;
						prev = perc;
					}
				}
				if(error)
					break;
			}
		}

		void performBFSWithSortedQ(string outFile)
		{
			cout << "\n Starting BFS on graph with " << numNodes << " nodes.\n";

			if(!lruCache)
			{
				cout << "\n Error : Cache not initialized for the graph..exiting";
				return;
			}
			
			Id processedNodeCount = 0;
			unmarkAllNodes(); // mark all nodes as ready
			BYTE *qBitSetForNodes = new BYTE[numOfBytesFornodeMarkerBitSet];
			//memset(qBitSetForNodes, 0, numOfBytesFornodeMarkerBitSet);
			Id startV;
			//vector<Id> bfsOutput;
			// ofstream bfsOutFile(outFile.c_str());
			bool error = false;
			int prev = -1;
			unsigned int bitSetIndex = 0;
			while(getFirstReadyNode(nodeMarkerBitSet, startV, bitSetIndex))
			{
				//cout << "\nStarting vertex : " << startV << "\n";
				//bfsOutFile << "\nStarting vertex : " << startV << "\n";
				//q.push(startV);
				memset(qBitSetForNodes, ~0, numOfBytesFornodeMarkerBitSet);
				unmarkNode(qBitSetForNodes, startV);
				markNode(nodeMarkerBitSet, startV);
				unsigned int qBitSetIndex = startV >> 3;
				
				Id onQNode;
				while(getFirstReadyNode(qBitSetForNodes, onQNode, qBitSetIndex))
				{
					const CDAGNode *curNode = lruCache->getData(onQNode);
					if(!curNode)
					{
						cout <<"\n Failed to get " << onQNode << " node..stopping BFS";
						error = true;
						break;
					}
					markNode(qBitSetForNodes, onQNode);
					//bfsOutput.push_back(curNode->dynId);
					//bfsOutFile << curNode->dynId << " ";
					for(std::deque<Id>::const_iterator it = curNode->succsList.begin();
						it != curNode->succsList.end(); ++it)
					{
						if(!isNodeMarked(nodeMarkerBitSet, *it))
						{
							//q.push(*it);
							unmarkNode(qBitSetForNodes, *it);
							markNode(nodeMarkerBitSet, *it);							
						}
						else
						{
							// its already visited
						}
					}
					++processedNodeCount;
					int perc = (processedNodeCount*100)/numNodes;
					if((perc % 1) == 0 && perc != prev)
					{
						cout << "\r\033[K ";
						cout << "Number of nodes processed : " << processedNodeCount;
						cout << " (of " << numNodes << ") - ";
						cout << perc << " % done.";
						cout << flush;
						prev = perc;
					}
				}
				if(error)
					break;
			}
			delete []qBitSetForNodes;
		}

		void performTopoSort(string outFile, bool useStack)
		{
			cout << "\n Starting Topological sort on graph with " << numNodes << " nodes.\n";

			if(!lruCache)
			{
				cout << "\n Error : Cache not initialized for the graph..exiting";
				return;
			}

			boost::unordered_map<Id, Id > nodeIdToUnprocSuccsCountMap;
			deque<Id> readyNodeQ;
			Id readyNodeCount = 0;
			Id processedNodeCount = 0;
			int prev = -1;
			
			// INITIAL SETUP

	        // Traverse over the graph once to mark all the nodes as ready
	        // that have 0 preds i.e. all input vertices
	        for(Id i=0; i<numNodes; ++i)
	        {
	            CDAGNode *node = this->getNode(i);
	            if(node->predsList.size() == 0)
	            {
	                // a node with zero predecessors
	                // it should be the load node
	                assert(node->type == 27); 
	                readyNodeQ.push_back(i);
	                ++readyNodeCount;
	            }
	            nodeIdToUnprocSuccsCountMap[i] = node->predsList.size();
	        }
			
			//ofstream tsOutFile(outFile.c_str());

			// PERFORM TOPOLOGICAL SORT
			int bitSetIndex = 0;
			while(readyNodeQ.size() > 0)
			{
				CDAGNode *node = 0;
				if(useStack)
				{
					node = this->getNode(readyNodeQ.back());
					readyNodeQ.pop_back();
				}
				else
				{
					node = this->getNode(readyNodeQ.front());
					readyNodeQ.pop_front();
				}
				++processedNodeCount;
				//tsOutFile << node->dynId << "\n";
				Id numSucc = node->succsList.size();
				for(Id i=0; i<numSucc; ++i)
				{
					Id succId = node->succsList[i];
					boost::unordered_map<Id, Id >::iterator it = nodeIdToUnprocSuccsCountMap.find(succId);
					if(it != nodeIdToUnprocSuccsCountMap.end())
					{
						--nodeIdToUnprocSuccsCountMap[succId];
						if(nodeIdToUnprocSuccsCountMap[succId] == 0)
						{
							readyNodeQ.push_back(succId);
							nodeIdToUnprocSuccsCountMap.erase(it);
						}
					}
				}

				int perc = (processedNodeCount*100)/numNodes;
				if((perc % 1) == 0 && perc != prev)
				{
					cout << "\r\033[K ";
					cout << "Number of nodes processed : " << processedNodeCount;
					cout << " (of " << numNodes << ") - ";
					cout << perc << " % done.";
					cout << flush;
					prev = perc;
				}

			}

			if(processedNodeCount != numNodes)
			{
				cout << "\n Error : Cycles found in the graph!";
			}
		}

	 	void printDiskGraph(ostream &out)
	 	{
	 		if(!lruCache)
	 		{
	 			cout << "\n Cannot print graph since cache is not initialized";
	 			return;
	 		}
	 		// Prints the complete graph with successor
	 		// and neighbor information (if available)
	 		for(Id i=0; i<numNodes; ++i)
	 		{
	 			CDAGNode *node = lruCache->getData(i);
	 			node->print(out);
	 			if(neighborCache)
	 			{
	 				neighborCache->getData(i)->print(out);
	 			}
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
				CDAGNode *node = lruCache->getData(i);
				file << "\tNode" << i << " [label=\"" << i << ". " << llvm::Instruction::getOpcodeName(node->type) << "\"];\n";
				size_t numPreds = node->predsList.size();
				for(size_t j=0; j<numPreds; j++)
				{
					file << "\tNode" << node->predsList[j] << " -> Node" << i << ";\n";
				}
			}

			file << "}";
			file.close();
		}

		// Disk CDAG block management
		void flushCurrentBlockToDisk(bool flag)
		{
			typename std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
	 		stringstream ss;
	 		int prev = -1;
	 		for(; it!=idToCDAGNodeMap.end(); ++it)
	 		{
	 			Id curId = it->second->getId() + 1;
	 			int perc = (curId*100)/count;
	 			if(perc%5 == 0 && prev != perc)
	 			{
	 				prev = perc;
	 				cout << "\r\033[K ";
	 				cout << "Processed node with id :" << curId;
	 				cout << " ( of " << count << "nodes) - ";
	 				cout << perc << "% done." << flush;
	 			}
	 			// it->second->writeToStream(ss);
	 			// graphDumpFile << ss.str();
	 			it->second->writeToStreamInBinary(graphDumpFile);
	 		}
	 		graphDumpFile.flush();

	 		// reset all the required variables after flushing to
			// disk
			resetGraphState();
		}

		// Reads a block from a given stream to the 
		// in memory map
		bool readBlockFromFile(istream &file)
		{
			//cout << "\n\n In readBlockFromFile \n";
			bool err = true;
			resetGraphState();

			streampos pos;
			while(!file.eof())
			{
				pos = file.tellg();
				CDAGNode *node = getAvailableCDAGNode();
				// err = node->readNodeFromASCIIFile(file);
				err = node->readNodeFromBinaryFile(file);
				curBlockSize += node->getSize(BLOCK_SIZE);//sizeof(*node);
				if(curBlockSize < BLOCK_SIZE*NUM_SLOTS && !file.eof())
				{
					idToCDAGNodeMap[node->dynId] = node;
				}
				else
				{
					availableCDAGNodesQ.push(node);
					// we have read an extra node move back 
					// if we have not reached the end of file
					// yet
					if(!file.eof())
					{
						file.seekg(pos, ios::beg);
					}
					break; //we have read the block
				}
			}
			//cout << "\n Exiting readBlockFromFile method with return value " << (err || file.eof())<< "\n\n";

			return (err || file.eof()); // return true if error or eof is reached
		}


	private:
		unsigned int addUpdateNodeToBlock(CDAGNode *node){
			// Check if user specified a block size.
			// If not then its a in memory CDAG.
			// just return.
			if(BLOCK_SIZE == 0)
			{
				idToCDAGNodeMap[node->dynId] = node; // just update the map
				return blockCount;
			}

			size_t nodeSize = node->getSize(BLOCK_SIZE); //sizeof(*node);
			assert(nodeSize > 0);

			Id nodeId = node->dynId;

			if(idToCDAGNodeMap.find(nodeId) != idToCDAGNodeMap.end())
			{
				// this is an existing node which is being updated
				if(nodeSize > CDAGNODE_SIZE)
				{
					// if the size of this node has grown bigger than the 
					// initial node size then just count the extra size by
					// which the node has grown
					nodeSize = nodeSize - CDAGNODE_SIZE;
					//continue
				}
				else
				{
					// Allocated space is enough. No block updates required.
					return blockCount;
				}

			}
			else
			{
				// this is a new node being added
				// continue.
			}

			// If we have reached here then either : 
			// A new node is being added
			// Or an existing node has grown in size.

			int newBlockSize = curBlockSize + nodeSize;

			if(newBlockSize < BLOCK_SIZE)
			{
				// if updated block size < current block size
				// then no change is needed
				curBlockSize = newBlockSize;
			}
			else
			{
				// we have exceeded the specified block size

				// check if we had partially created this node
				// in the current block. This is a "spill node"
				// which is partially extending to next block as well.

				// If yes then we need to remove this node before
				// flushing the map out to disk.
				if(idToCDAGNodeMap.find(nodeId) != idToCDAGNodeMap.end())
				{
					idToCDAGNodeMap.erase(nodeId);
				}
				else
				{
					// this is a brand new node
				}

				// flush the current block to the disk
				flushCurrentBlockToDisk(false);

				// start using a new block now
				++blockCount;

				// update curBlockSize
				curBlockSize = node->getSize(BLOCK_SIZE); //sizeof(*node);
			}
			idToCDAGNodeMap[nodeId] =  node;

			return blockCount;
		}


		void resetGraphState(bool delNodes = false)
		{
			typename std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
			for(; it!=idToCDAGNodeMap.end(); ++it)
			{
				delNodes ? delete it->second :availableCDAGNodesQ.push(it->second);
				it->second = 0;
			}
			idToCDAGNodeMap.clear();
			curBlockSize = 0;
		}

	public:
		DiskCache<CDAGNode, Id> *lruCache;
		// Other API  : NOT YET IMPLEMENTED
		//Returns the successor list of 'nodeId'
		CDAGNode* getNode(const Id &nodeId)
		{
			CDAGNode *retVal = 0;
			if(lruCache)
			{
				retVal = lruCache->getData(nodeId);
			}
			else
			{
				cout << "\nError : cannot get node with Id :" << nodeId ;
				cout << " because LRU cache for the disk graph is not initialized";
			}
			return retVal;
		}

		NodeNeighborInfo* getNeighbor(const Id &nodeId)
		{
			NodeNeighborInfo* retVal = 0;
			if(neighborCache)
			{
				retVal = neighborCache->getData(nodeId);
			}
			return retVal;
		}
		void getSuccessors(size_t nodeId, const size_t *&list, size_t &size)
		{
			assert(nodeId < numNodes);
		}

		//Returns the predecessor list of 'nodeId'
		void getPredecessors(size_t nodeId, const size_t *&list, size_t &size)
		{
			assert(nodeId < numNodes);
		}

		//Returns no. of successors of 'nodeId'
		size_t getNumSuccessors(size_t nodeId)
		{
			assert(nodeId < numNodes);
		}

		//Returns no. of predecessors of 'nodeId'
		size_t getNumPredecessors(size_t nodeId)
		{
			assert(nodeId < numNodes);
		}


		//Returns True if 'nodeId' is input node, false otherwise
		bool isInputNode(size_t nodeId)
		{
		
		}

		//Returns True if 'nodeId' is output node, false otherwise
		bool isOutputNode(size_t nodeId)
		{
			
		}
		size_t addNode(unsigned int currType, size_t id, Address addr);
};

//Adds a node to the cdag with instruction type 'currType' and static id 'id'
template<typename CDAGNode>
size_t DiskCDAG<CDAGNode>::addNode(unsigned int currType, size_t id, Address addr)
{
	assert(numNodes < count);

	CDAGNode *node = getAvailableCDAGNode();
	node->dynId = numNodes;
	node->type = currType;
	node->staticId = id;
	if(currType == Instruction::Load){
		// addr is only consumed in case of load instruction
		node->addr = addr;
	}
	else
	{
		readAddressFromFile(node->dynId, node->addr);
		if(node->addr == 0)
		{
			node->addr = node->staticId;
		}
	}

	// if(boost::is_same<CDAGNode, GraphNodeWithIV>::value)
	// {
	// 	node->loopItVec.insert(node->loopItVec.begin(), curItVec.begin(), curItVec.end());
	// }

	addUpdateNodeToBlock(node);
	return numNodes++;
}

//Adds a node to the cdag with instruction type 'currType' and static id 'id'
template<>
size_t DiskCDAG<GraphNodeWithIV>::addNode(unsigned int currType, size_t id, Address addr)
{
	assert(numNodes < count);

	GraphNodeWithIV *node = getAvailableCDAGNode();
	node->dynId = numNodes;
	node->type = currType;
	node->staticId = id;
	if(currType == Instruction::Load){
		// addr is only consumed in case of load instruction
		node->addr = addr;
	}
	else
	{
		readAddressFromFile(node->dynId, node->addr);
		if(node->addr == 0)
		{
			node->addr = node->staticId;
		}
	}
	
	if(curIV.size() > 0)
	{
		node->loopItVec.insert(node->loopItVec.begin(), curIV.begin(
			), curIV.end());
		++curIV[curIV.size()-1];
	}

	addUpdateNodeToBlock(node);
	return numNodes++;
}

#ifdef FULL_DAG
template <typename Node>
void DiskCDAGBuilder<Node>::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
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
				size_t inputNodeId = cdag ? cdag->addNode(Instruction::Load, 
					cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
					addr) : incNumNodesCounter(addr);
				loadMap.insert(make_pair(addr, inputNodeId));
				tempId = inputNodeId;
			}
			else
			{
				tempId = it->second;
			}
			predSet.insert(tempId);
			if(successorCountFile)
			{
				// we are writing successor count to file
				incSuccessorCountInFile(tempId);
			}
		}
	}

	// Store instruction handling for getting addresses.
	// Only do it for the first pass
	if(!cdag && isa<StoreInst>(instr))
	{
		assert(predecessors.size() <= 2);
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			payload_type pred_data = (*pred)->get();
			if(isa<GetElementPtrInst>((*pred)->getInstruction()))
			{
				continue;
			}
			else
			{
				updateNodeAddress(pred_data, node->getAddress());
			}
		}
	}

	//Create a cdag node
	size_t nodeId = cdag ? cdag->addNode(instr->getOpcode(),
		cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
		0) : incNumNodesCounter(cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
	for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
	{
		payload_type pred_data = (*pred)->get();
		predSet.insert(pred_data);
		if(successorCountFile)
		{
			// we are writing successor count to file
			incSuccessorCountInFile(pred_data);
		}
	}
	if(cdag) cdag->setPredecessor(nodeId, predSet);
	node->set(nodeId);
}
#else
template <typename Node>
void DiskCDAGBuilder<Node>::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
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
				size_t inputNodeId = cdag ? cdag->addNode(Instruction::Load, 
					cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
					addr) : incNumNodesCounter(addr);
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

	// Store instruction handling for getting addresses.
	// Only do it for the first pass
	if(!cdag && isa<StoreInst>(instr))
	{
		assert(predecessors.size() <= 2);
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			payload_type pred_data = (*pred)->get();
			if(isa<GetElementPtrInst>((*pred)->getInstruction()))
			{
				continue;
			}
			else
			{
				payload_type::iterator plIt = pred_data.begin();
				for(; plIt != pred_data.end(); ++plIt)
				{
					updateNodeAddress(*plIt, node->getAddress());
				}
			}
		}
	}

	if(instr->isBinaryOp() && instr->getType()->isFloatingPointTy())
	{
		//Create a cdag node
		size_t nodeId = cdag ? cdag->addNode(instr->getOpcode(), 
			cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
			0) : 
			incNumNodesCounter(cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue());
		for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
		{
			payload_type pred_data = (*pred)->get();
			predSet.insert(pred_data.begin(), pred_data.end());
			if(successorCountFile)
			{
				// we are writing successor count to file
				std::set<size_t>::iterator setIt = pred_data.begin();
				for(; setIt != pred_data.end(); ++setIt)
				{
					incSuccessorCountInFile(*setIt);
				}
			}
		}
		if(cdag) cdag->setPredecessor(nodeId, predSet);
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

template <typename Node>
void DiskCDAGBuilder<Node>::incSuccessorCountInFile(Id nodeId)
{
	streampos pos(sizeof(Id)*nodeId);
	successorCountFile.seekg(pos, ios::beg);
	Id curVal = 0;
	successorCountFile.read((char*)&curVal, sizeof(Id));

	curVal += 1;

	successorCountFile.seekp(pos, ios::beg);
	successorCountFile.write((const char*)&curVal, sizeof(Id));
	successorCountFile.flush();// Do not remove this. Write 0 for the second last node
}

template <typename Node>
void DiskCDAGBuilder<Node>::updateNodeAddress(Id nodeId, Address addr)
{	
	streampos pos(sizeof(Address)*nodeId);
	nodeAddrFile.seekp(pos, ios::beg);
	nodeAddrFile.write((const char*)&addr, sizeof(Address));
	nodeAddrFile.flush();
}

template <typename Node>
size_t DiskCDAGBuilder<Node>::incNumNodesCounter(Address addr)
{
	if(successorCountFile && nodeAddrFile)
	{
		Id temp = 0;
		successorCountFile.seekp(0, fstream::end); // write to the end of the file
		successorCountFile.write((const char*)&temp, sizeof(Id));
		successorCountFile.flush(); 
		Address addr = 0;
		nodeAddrFile.seekp(0, fstream::end);
		nodeAddrFile.write((const char*)&addr, sizeof(Address));
	}
	return numNodes++;
}

template <typename Node>
void DiskCDAGBuilder<Node>::printSuccessorCountFile()
{
	Id temp = 0, i=0;
	cout <<"\n";
	successorCountFile.seekg(0, fstream::beg);
	while(!successorCountFile.read((char*)&temp, sizeof(Id)).eof())
	{
		cout <<"\n" << i++ << " : " << temp;
	}
	cout <<"\n";
}

template <typename Node>
void DiskCDAGBuilder<Node>::printAddressFile()
{
	Address addr = 0;
	Id i=0;
	nodeAddrFile.seekg(0, fstream::beg);
	while(!nodeAddrFile.read((char*)&addr, sizeof(Address)).eof())
	{
		cout << "\n" << i++ << " : " <<addr;
	}
	cout << "\n";
}


void DiskCDAGBuilderWithIV::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
{
	// if(curIV.size() > 0)
	// {
	// 	++curIV.back().stmtNum;
	// }
	// if(cdag)
	// {
	// 	cdag->curItVec.clear();
	// 	for(int i=0; i<curIV.size(); ++i)
	// 	{
	// 		cdag->curItVec.push_back(curIV[i].loopId);
	// 		cdag->curItVec.push_back(curIV[i].stmtNum);
	// 	}
	// }

	DiskCDAGBuilder<GraphNodeWithIV>::visitNode(node, predecessors, operands);
}

void printCurIV(deque<Id> &curIV)
{
	cout << "\n";
	for(int i=0; i<curIV.size(); ++i)
	{
		cout << curIV[i] << " ";
	}
}
void DiskCDAGBuilderWithIV::visitLoopBegin(Id loopId)
{
	curIV.push_back(0);
	curIV.push_back(0);
	curDepth++;
	if(cdag && cdag->maxDepth < curDepth)
	{
		cdag->maxDepth = curDepth;
	}	
	//cout << "\n Loop Begin with ID :  " << loopId;
	//printCurIV(curIV);
}

void DiskCDAGBuilderWithIV::visitLoopIterBegin()
{
	assert(curIV.size() > 0 );
	curIV[curIV.size()-1] = 0; // reset the statement number column
	//cout << "\n Loop iter begin ";
	//printCurIV(curIV);
}

void DiskCDAGBuilderWithIV::visitLoopIterEnd()
{
	assert(curIV.size() > 0 );
	++curIV[curIV.size()-2]; // increment the column representing induction variable
	//cout << "\n Loop iter end ";
	//printCurIV(curIV);
}

void DiskCDAGBuilderWithIV::visitLoopEnd(Id loopId)
{
	assert(curIV.size() > 0);
	// delete last_loopinfo;
	curIV.pop_back();
	curIV.pop_back();
	curDepth--;
	//cout << "\n Loop end with ID :  " << loopId;
	//printCurIV(curIV);
}

}

#endif