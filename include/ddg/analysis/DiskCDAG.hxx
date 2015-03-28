#ifndef DiskCDAG_HXX
#define DiskCDAG_HXX
#endif

//#define FULL_DAG //If defined, whole ddg is built, else just the CDAG is built

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
#include <sstream>
#include <istream>

#include "ddg/analysis/DiskCache.hxx"

namespace ddg{
	typedef unsigned char BYTE;
}

namespace utils{

using namespace std;
using namespace ddg;

// Method for calculating storage space needed to represent
// each node by a bit.
inline int convertNumNodesToBytes(const int &numNodes)
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

#define NUM_SLOTS 8

#ifdef FULL_DAG
typedef size_t payload_type;
#else
typedef std::set<size_t> payload_type;
#endif

class DiskCDAGBuilder;
class DiskCDAG;

// Temporary files used for graph generation
const string tempSuccsCountFNSuffix = "succscounttemp";
const string tempGraphDumpFNSuffix = "graphdump";
const string tempSuccsListFNSuffix = "succslisttemp";

// Final dump of graph
const string diskGraphFNSuffix = "diskgraph";
const string diskGraphIndexFNSuffix = "diskgraph_index";




class DiskCDAGBuilder: public LazyGraphVisitor<payload_type>
{
	protected:
		DiskCDAG *cdag;
		map<Address, size_t> loadMap; //Maps the memory address to ddg input node id
		bool getCountsFlag;
		Id numNodes;
		string bcFileName;
		fstream successorCountFile;

	public:
		DiskCDAGBuilder(DiskCDAG *cdag) :	cdag(cdag), loadMap(), 
											getCountsFlag(false), numNodes(0),
											successorCountFile("")
		{

		}

		DiskCDAGBuilder() : cdag(0), loadMap(), getCountsFlag(true), 
							numNodes(0), successorCountFile("")
		{

		}

		DiskCDAGBuilder(string fileName) : cdag(0), loadMap(), getCountsFlag(true), 
											numNodes(0), bcFileName(fileName)
		{
			successorCountFile.open((bcFileName+tempSuccsCountFNSuffix).c_str(), std::fstream::out | std::fstream::trunc);
			successorCountFile.close();
			successorCountFile.open((bcFileName+tempSuccsCountFNSuffix).c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
		}

		Id getNumNodes(){return numNodes;}

		virtual void visitNode(LazyNode<payload_type> *node, Predecessors	&predecessors, Operands &operands);
		void incSuccessorCountInFile(Id nodeId);
		size_t incNumNodesCounter();
		void printSuccessorCountFile();
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
		for(int i=0; i<(listCapacity-list.size()); ++i)
		{
			file.write((const char*)&temp, sizeof(Id));
		}
	}

	bool readNodeFromASCIIFile(istream &file)
	{
		listCapacity = 0;
		file.read((char*)&id, sizeof(Id));
		file.read((char*)&listCapacity, sizeof(Id));		
		for(int i=0; i<listCapacity;++i)
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
		for(int i=0; i<listCapacity;++i)
		{
			Id temp =0;
			file.read((char*)&temp, sizeof(Id));
			if(temp > 0)
			{
				list.push_back(temp);
			}
		}
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

};
class DiskCDAG
{
	private:
		struct CDAGNode{
			Id dynId;	// Dynamic Id 
			Address addr;	// Addresses represented by this node if any
			unsigned int type;	// LLVM Type of the node
			Id staticId;
			std::vector<Id> predsList;	// Vector containing list of predecessors
			std::vector<Id> succsList;	// Vector containing list of successors

			// TODO : initialize it with proper values
			CDAGNode(): dynId(0),
						addr(0),
						type(0),
						staticId(0)
			{
			}

			Id getId(){return dynId;}

			void print(ostream &out) const
			{
				out << "\n" << dynId << ". ";
				out << "Instruction :" << llvm::Instruction::getOpcodeName(type) << " ";
				out << "StaticID : " << staticId << " ";
				out << " \n Num Predecessors: " << predsList.size() <<"\n";
				for(std::vector<Id>::const_iterator it = predsList.begin();
					it != predsList.end();
					++it)
				{
					out << *it << ",";
				}
				out << " \n Num Successor: " << succsList.size() <<"\n";
				for(std::vector<Id>::const_iterator it = succsList.begin();
					it != succsList.end();
					++it)
				{
					out << *it << ",";
				}
				out << "\n------------------------------------------------------\n";
			}

			// TODO : try taking in a output stream directly
			// 		  instead of returning a string
			void writeToStream(stringstream &ss)
			{
				ss.str(std::string());				
				ss << dynId << " ";
				ss << staticId << " ";
				ss << type << " ";
				ss << addr << " ";
				ss << "\n";
				
				ss << predsList.size() << " ";
				for(std::vector<Id>::iterator it = predsList.begin();
					it != predsList.end();
					++it)
				{
					ss << *it << " ";
				}
				ss << "\n";

				ss << succsList.size() << " ";
				for(std::vector<Id>::iterator it = succsList.begin();
					it != succsList.end();
					++it)
				{
					ss << *it << " ";
				}
				ss << "\n";
			}

			void writeToStreamInBinary(ostream &file)
			{
				file.write((const char*)&dynId, sizeof(Id));
				file.write((const char*)&staticId, sizeof(Id));
				file.write((const char*)&type, sizeof(unsigned int));
				file.write((const char*)&addr, sizeof(Address));

				Id predCount = predsList.size();
				file.write((const char*)&predCount, sizeof(Id));
				for(std::vector<Id>::iterator it = predsList.begin();
					it != predsList.end();
					++it)
				{
					Id temp = *it;
					file.write((const char*)&temp, sizeof(Id));
				}
				// copy(predsList.begin(), predsList.end(), ostream_iterator<Id>(file));

				Id succCount = succsList.size();
				file.write((const char*)&succCount, sizeof(Id));
				for(std::vector<Id>::iterator it = succsList.begin();
					it != succsList.end();
					++it)
				{
					Id temp = *it;
					file.write((const char*)&temp, sizeof(Id));
				}
				//copy(succsList.begin(), succsList.end(), ostream_iterator<Id>(file));
			}

			void writeToStream(fstream &ss)
			{

			}

			bool readNodeFromBinaryFile(istream &file)
			{
				file.read((char*)&dynId, sizeof(Id));
				file.read((char*)&staticId, sizeof(Id));
				file.read((char*)&type, sizeof(unsigned int));
				file.read((char*)&addr, sizeof(Address));

				Id predCount = 0;
				file.read((char*)&predCount, sizeof(Id));
				for(int i=0; i < predCount; ++i)
				{
					Id temp = 0;
					file.read((char*)&temp, sizeof(Id));
					predsList.push_back(temp);
					// TODO : how to make this cleaner implementation work?
					//file.read((char*)&predsList[i], sizeof(Id)); 
				}

				Id succCount = 0;
				file.read((char*)&succCount, sizeof(Id));
				for(int i=0; i < succCount; ++i)
				{
					Id temp = 0;
					file.read((char*)&temp, sizeof(Id));
					succsList.push_back(temp);
				}
			}

			bool readNodeFromASCIIFile(istream &file)
			{
				
				

				// Read dynId, static Id, type and addr
				{
					std::string temp, line;
					std::stringstream tempss;
					getline(file, line);
					tempss << line;
					getline(tempss, temp, ' ');
					dynId = atoi(temp.c_str());
					getline(tempss, temp, ' ');
					staticId = atoi(temp.c_str());
					getline(tempss, temp, ' ');
					type = atoi(temp.c_str());
					getline(tempss, temp, ' ');
					addr = atol(temp.c_str());
				}

				// Read the predecessors
				{
					std::string temp, line;
					std::stringstream tempss;
					getline(file, line);
					tempss.str(string());
					tempss.str(line);
					getline(tempss, temp, ' '); // read the count
					while(getline(tempss, temp, ' ')) // start reading the preds
					{
						predsList.push_back(atoi(temp.c_str()));
					}
				}

				// Read the successors
				{
					std::string temp, line;
					std::stringstream tempss;
					getline(file, line);
					tempss.str(string());
					tempss.str(line);
					getline(tempss, temp, ' '); // read the count
					while(getline(tempss, temp, ' ')) // start reading the succs
					{
						succsList.push_back(atoi(temp.c_str()));
					}		
				}

				return false; // TODO compelete this. Return true if errors
			}

			void reset()
			{
				dynId = 0;
				staticId = 0;
				addr = 0;
				type = 0;
				predsList.clear();
				succsList.clear();
			}
		};

		static const size_t CDAGNODE_SIZE = sizeof(CDAGNode);

		BYTE *succsBitSet;
		BYTE *nodeMarkerBitSet;
		size_t numOfBytesForSuccBS;
		size_t numOfBytesFornodeMarkerBitSet;


		size_t blockSize;
		size_t blockCount;
		size_t curBlockSize;

		std::map<Id, CDAGNode*> idToCDAGNodeMap;

		fstream graphDumpFile;
		fstream succsListTempFile;
		fstream succsListNewTempFile;

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
		DiskCache<CDAGNode, Id> *lruCache;

		queue<CDAGNode*> availableCDAGNodesQ;

		bool graphCreatedFlag;

		void init(size_t count)
		{
			lruCache = 0;
			numOfBytesForSuccBS = utils::convertNumNodesToBytes(count);
			succsBitSet = new BYTE[numOfBytesForSuccBS];
			nodeMarkerBitSet = 0; // to be initialized if we get actual node count
			memset(succsBitSet, 0, numOfBytesForSuccBS);

			// TODO : compare with CDAGNODE_WITHSUCC_SIZE
			if(blockSize != 0 && blockSize < CDAGNODE_SIZE)
				cout << "\n Block size is less than CDAG node size..Aborting!";
			else if(blockSize == 0){
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
				if(diskGraphFileName.empty())
				{
					cout << "\n Error empty disk graph filename passed.";
					exit(1); // TODO : set error flag instead of exiting
				}
				// graph is already created and the filename is passed along

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
			
			//printBeforeWriteFile.open(string(bcFileName+"printbeforewrite").c_str());
			//printAfterReadFile.open(string(bcFileName+"printafterread").c_str());

			// // dumping blocks of memory to the file
			// cout <<"\n Dumping blockss for succesor file" <<flush;
			// for(int i=0; i < count; ++i)
			// {
			// 	succsListTempFile.write((const char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));
			// }
			// cout << "\n Done creating the successor file" <<flush;

			// initialize the new successor list file
			initSuccessorListFile();

			succsCache = new DiskCache<DataList, Id>(blockSize, NUM_SLOTS, true);
			if(!succsCache->init(bcFileName+tempSuccsListFNSuffix))
			{
				cout << "\n Cache initialization for successor's list failed...";
				return;
			}
			// cout << "\n Printing succs list from cache";
			// cin.get();
			// for(int i=0; i<count; ++i)
			// {
			// 	DataList* l = succsCache->getData(i);
			// 	l->print(cout);
			// }
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
				for(int i=0; i<succCount; ++i)
				{
					succsListTempFile.write((const char*)&temp, sizeof(Id));
				}
				++id;
			}
			succsListTempFile.close();

			// Delete the temp count file
			remove(succsCountTempFileName.c_str());

			// // TODO : Test code
			// {
			// 	DiskCache<DataList, Id> succsCache(blockSize, 2, true);
			// 	if(!succsCache.init(bcFileName+"succslistnewtemp"))
			// 	{
			// 		cout << "\n Cache initialization for successor's list failed...";
			// 		return;
			// 	}
			// 	cout << "\n Printing succs list from cache";
			// 	cin.get();
			// 	for(int i=0; i<count; ++i)
			// 	{
			// 		DataList* l = succsCache.getData(i);
			// 		l->print(cout);
			// 	}
			// 	DataList* l = succsCache.getData(1610);
			// 	l->print(cout);
			// 	l->addToList(5);
			// 	l = succsCache.getData(1610);
			// 	l->print(cout);
			// }
			// cin.get();

			// {
			// 	DiskCache<DataList, Id> succsCache(blockSize, 2);
			// 	cin.get();
			// 	if(!succsCache.init(bcFileName+"succslistnewtemp"))
			// 	{
			// 		cout << "\n Cache initialization for successor's list failed...";
			// 		return;
			// 	}
			// 	for(int i=0; i<count; ++i)
			// 	{
			// 		DataList* l = succsCache.getData(i);
			// 		l->print(cout);
			// 	}
			// 	cout << " \n\n Testing Write back to file";
			// 	DataList* l = succsCache.getData(1610);
			// 	l->print(cout);
			// }

			// cin.get();
		}

		void initLRUCacheForDiskGraph()
		{
			if(!lruCache)
			{
				cout << "\n Initialize LRU cache";
	      		lruCache = new DiskCache<CDAGNode, Id>(blockSize, NUM_SLOTS);
				//if(!lruCache->init(diskGraphFileName, diskGraphIndexFileName ))
				if(!lruCache->init(diskGraphFileName))
				{
					cout <<"\n Cache initialization failed..stopping BFS";
					return;
				}
				cout << "\n LRU Disk Cache initialized.";
			}
		}

	public:
		DiskCDAG(size_t count) : numNodes(0),
								 count(count),
								 blockSize(0),
								 blockCount(0),
								 curBlockSize(0)
		{
			graphCreatedFlag = false;
			init(count);
		}

		DiskCDAG(Ids &ids, const string &bcFile, 
			size_t bs, Id nodeCount) : numNodes(0),
									 	blockSize(bs*1024), // block size is passed in MB
									 	//blockSize(bs),
									 	blockCount(0),
									 	curBlockSize(0),
									 	bcFileName(bcFile),
									 	count(nodeCount)
		{
			graphCreatedFlag = false;
			//CDAGCounter counter(ids); //Get the count of expected no. of nodes
			//count = counter.getCount();
			init(count);
		}

		DiskCDAG(Ids &ids, const string &bcFile,
			const string &graphFileName, 
			const string &graphIndexFileName,
			size_t bs) : numNodes(0),
									 	blockSize(bs*1024), // block size is passed in MB
									 	//blockSize(bs),
									 	blockCount(0),
									 	curBlockSize(0),
									 	bcFileName(bcFile),
									 	count(0)
		{
			graphCreatedFlag = true;
			diskGraphFileName = graphFileName;
			diskGraphIndexFileName = graphIndexFileName;
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
			std::map<Id, CDAGNode*>::iterator it1 = idToCDAGNodeMap.begin();
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

		//Adds a node to the cdag with instruction type 'currType' and static id 'id'
		size_t addNode(unsigned int currType, size_t id, Address addr)
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

			addUpdateNodeToBlock(node);
			return numNodes++;
		}

		void setPredecessor(size_t nodeId, const set<size_t> &predSet)
		{
			assert(nodeId < numNodes);
			size_t size = predSet.size();
			set<size_t>::const_iterator it = predSet.begin();
			for(size_t i=0; i<size; ++i)
			{
				idToCDAGNodeMap[nodeId]->predsList.push_back(*it);
				//writeSuccessorInFile((*it), nodeId);
				//updateSuccessorCountInFile((*it), 1); // bump up the count by 1
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

		

		void cleanUpTemporaries()
		{
			// Cleaning up the temporary disk files and data structures
			cout <<"\n Cleaning up the temporary files and data structures now...";
			if(graphDumpFile)
			{
				graphDumpFile.close();
			}
			remove((bcFileName+tempGraphDumpFNSuffix).c_str());
			resetGraphState(true);


			if(succsCache)
			{
				delete succsCache;
				succsCache = 0;
			}
			remove((bcFileName+tempSuccsListFNSuffix).c_str());

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
			size_t bs = blockSize*NUM_SLOTS;

			graphDumpFile.clear();
	 		graphDumpFile.seekg(0, ios::beg);
	 		blockCount = 0;

	 		//cin.get();

			while(!readBlockFromFile(graphDumpFile))
			{
				writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex, 
					blockSize, pos, curBlockSize);
				++blockCount;
			}

			writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex,
			 blockSize, pos, curBlockSize);
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
			
			map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
			for(; it != idToCDAGNodeMap.end(); ++it)
			{
				//readSuccessorsFromFile(it->first, succList);
				succList.clear(); // optimization : use a pointer to the vector
				DataList * l = succsCache->getData(it->first);
				CDAGNode *curNode = it->second;
				if(l->list.size() > 0)
				{
					curNode->succsList = l->list;
				}

				// Check if we reached the write block size
				size_t curNodeSize = sizeof(*curNode);
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
			vector<CDAGNode*>::iterator it1 = nodesToWriteList.begin();
			int prev = -1;
			for(; it1 != nodesToWriteList.end(); ++it1)
			{
				// (*it1)->writeToStream(ss);
				// diskGraph << ss.str();
				(*it1)->writeToStreamInBinary(diskGraph);

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

		//Generates the DiskCDAG using DiskCDAGBuilder
		static DiskCDAG* generateGraph(Ids& ids, const string &bcFileName, 
			const string graphFileName, const string graphIndexFileName,
			int block_size)
		{
			DiskCDAG *cdag;

			if(graphFileName.empty())
			{
				cout <<"\n-----Starting First pass over the trace to get counts." << flush;
				DiskCDAGBuilder countBuilder(bcFileName);
				countBuilder.visit(ids);
				cout <<"\n-----Pass complete. Number of nodes : "<<countBuilder.getNumNodes() << flush;
				//countBuilder.printSuccessorCountFile();
				//cin.get();

				cdag = new DiskCDAG(ids, bcFileName, 
					block_size, countBuilder.getNumNodes());

				cout << "\n \n ";
				cout <<"\n-----Starting Second Pass over the trace to dump out the graph.\n";
				cout << "\n Step 1 (of 2) : Writing temporary graphdump and successor information now!\n"<<flush;			
				DiskCDAGBuilder builder(cdag);
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
      			cdag = new DiskCDAG(ids, bcFileName, graphFileName,
      				graphIndexFileName, block_size);
      		}

      		cdag->initLRUCacheForDiskGraph();

			return cdag;
		}

		//Generates the DiskCDAG using user-specified 'Builder'
		template <typename Builder>
			static DiskCDAG* generateGraph(Ids& ids, string &bcFileName, int block_size)
			{
				cout <<"\n First pass through trace to get counts" << flush;
				DiskCDAGBuilder countBuilder(bcFileName);
				countBuilder.visit(ids);
				cout <<"\n Pass complete. Number of nodes : "<<countBuilder.getNumNodes() << flush;
				//countBuilder.printSuccessorCountFile();

				DiskCDAG *cdag = new DiskCDAG(ids, bcFileName, 
				block_size, countBuilder.getNumNodes());

				cout << "\n Writing graphdump now!"<<flush;
				Builder builder(cdag);

				builder.visit(ids);
				cout << "\n Done writing graphdump now!" <<flush;

				return cdag;
			}

		void performBFS()
		{
			cout << "\n Starting BFS on graph with " << numNodes << " nodes.\n";

			if(!lruCache)
			{
				cout << "\n Error : Cache not initialized for the graph..exiting";
				return;
			}
			
			Id processedNodeCount = 0;
			unmarkAllNodes(); // mark all nodes as ready
			queue<Id> q;
			Id startV;
			//vector<Id> bfsOutput;
			ofstream bfsOutFile("bfsOut");
			bool error = false;
			while(getFirstReadyNode(nodeMarkerBitSet, startV))
			{
				bfsOutFile << "\nStarting vertex : " << startV << "\n";
				q.push(startV);
				markNode(nodeMarkerBitSet, startV);
				
				while(!q.empty())
				{
					const CDAGNode *curNode = lruCache->getData(q.front());
					if(!curNode)
					{
						cout <<"\n Failed to get " << q.front() << " node..stopping BFS";
						error = true;
						break;
					}
					q.pop();
					//bfsOutput.push_back(curNode->dynId);
					bfsOutFile << curNode->dynId << " ";
					for(vector<Id>::const_iterator it = curNode->succsList.begin();
						it != curNode->succsList.end(); ++it)
					{
						if(!isNodeMarked(nodeMarkerBitSet, *it))
						{
							q.push(*it);
							markNode(nodeMarkerBitSet, *it);							
						}
						else
						{
							// its already visited
						}
					}
					++processedNodeCount;
					int perc = (processedNodeCount*100)/numNodes;
					int prev = 0;
					if(perc % 5 == 0 && perc != prev)
					{
						cout << "\r\033[K ";
						cout << "Number of nodes processed : " << processedNodeCount;
						cout << " (of " << numNodes << ") - ";
						cout << perc << " % done.";
						cout << " | Number of (currently) discovered but unprocessed nodes : ";
						cout << q.size() << flush;
						prev = perc;
					}
				}
				// cout << "\n Listing reachable nodes in BFS order : \n";
				// for(vector<Id>::iterator it = bfsOutput.begin(); 
				// 	it != bfsOutput.end(); ++it)
				// {
				// 	cout << *it << " ";
				// }
				// cout <<"\n";
				// bfsOutput.clear();
				if(error)
					break;
			}
		}

		void performBFSWithoutQ()
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
			ofstream bfsOutFile("bfsOut");
			bool error = false;
			int prev = -1;
			while(getFirstReadyNode(nodeMarkerBitSet, startV))
			{
				//cout << "\nStarting vertex : " << startV << "\n";
				bfsOutFile << "\nStarting vertex : " << startV << "\n";
				//q.push(startV);
				memset(qBitSetForNodes, ~0, numOfBytesFornodeMarkerBitSet);
				unmarkNode(qBitSetForNodes, startV);
				markNode(nodeMarkerBitSet, startV);
				
				Id onQNode;
				while(getFirstReadyNode(qBitSetForNodes, onQNode))
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
					bfsOutFile << curNode->dynId << " ";
					for(vector<Id>::const_iterator it = curNode->succsList.begin();
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
					if((perc % 5) == 0 && perc != prev)
					{
						cout << "\r\033[K ";
						cout << "Number of nodes processed : " << processedNodeCount;
						cout << " (of " << numNodes << ") - ";
						cout << perc << " % done.";
						cout << " | Number of (currently) discovered but unprocessed nodes : ";
						cout << flush;
						prev = perc;
					}
				}
				// cout << "\n Listing reachable nodes in BFS order : \n";
				// for(vector<Id>::iterator it = bfsOutput.begin(); 
				// 	it != bfsOutput.end(); ++it)
				// {
				// 	cout << *it << " ";
				// }
				// cout <<"\n";
				// bfsOutput.clear();
				if(error)
					break;
			}
			delete []qBitSetForNodes;
		}

	 	void printDiskGraph(ostream &out)
	 	{
	 		std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
	 		//cout <<"\n\n " ;
	 		for(; it!=idToCDAGNodeMap.end(); ++it)
	 		{
	 			it->second->print(out);
	 		}
	 	}

		//Prints the graph in dot format
		void printDOTGraph(const char *filename)
		{
			// ofstream file;
			// file.open(filename);
			// file << "digraph \"ddg\" {\n";

			// for(size_t i=0; i<numNodes; i++)
			// {
			// 	file << "\tNode" << i << " [label=\"" << i << ". " << llvm::Instruction::getOpcodeName(type[i]) << "\"];\n";
			// 	size_t numPreds = predCnt[i];
			// 	for(size_t j=0; j<numPreds; j++)
			// 	{
			// 		file << "\tNode" << predList[i][j] << " -> Node" << i << ";\n";
			// 	}
			// }

			// file << "}";
			// file.close();
		}

		//Prints the graph in YAML format
		void printYAMLGraph(const char *filename)
		{
			// ofstream file;
			// file.open(filename);

			// file << "Nodes: \n";
			// for(size_t i=0; i<numNodes; ++i)
			// {
			// 	file << "  " << i << ": {null: 0}\n";
			// }
			// file << "Edges: \n";
			// for(size_t i=0; i<numNodes; ++i)
			// {
			// 	size_t numScsrs = scsrCnt[i];
			// 	if(numScsrs == 0)
			// 		continue;
			// 	file << "  " << i << ": {";
			// 	for(size_t j=0; j<numScsrs-1; ++j)
			// 	{
			// 		file << scsrList[i][j] << ": 1, ";
			// 	}
			// 	file << scsrList[i][numScsrs-1] << ": 1}\n";
			// }
			// file.close();
		}

		// Disk CDAG block management

		void flushCurrentBlockToDisk(bool flag)
		{
			std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
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
				curBlockSize += sizeof(*node);
				if(curBlockSize < blockSize*NUM_SLOTS && !file.eof())
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
			if(blockSize == 0)
			{
				idToCDAGNodeMap[node->dynId] = node; // just update the map
				return blockCount;
			}

			size_t nodeSize = sizeof(*node);
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

			if(newBlockSize < blockSize)
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
				curBlockSize = sizeof(*node);
			}
			idToCDAGNodeMap[nodeId] =  node;

			return blockCount;
		}


		void resetGraphState(bool delNodes = false)
		{
			std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
			for(; it!=idToCDAGNodeMap.end(); ++it)
			{
				delNodes ? delete it->second :availableCDAGNodesQ.push(it->second);
				it->second = 0;
			}
			idToCDAGNodeMap.clear();
			curBlockSize = 0;
		}

	public:
		// Other API  : NOT YET IMPLEMENTED
		//Returns the successor list of 'nodeId'
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
};

#ifdef FULL_DAG
void DiskCDAGBuilder::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
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
					addr) : incNumNodesCounter();
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

	//Create a cdag node
	size_t nodeId = cdag ? cdag->addNode(instr->getOpcode(),
		cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
		0) : incNumNodesCounter();
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
void DiskCDAGBuilder::visitNode(LazyNode<payload_type> *node, Predecessors &predecessors, Operands &operands)
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
					addr) : incNumNodesCounter();
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
		size_t nodeId = cdag ? cdag->addNode(instr->getOpcode(), 
			cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
			0) : incNumNodesCounter();
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

void DiskCDAGBuilder::incSuccessorCountInFile(Id nodeId)
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

size_t DiskCDAGBuilder::incNumNodesCounter()
{
	if(successorCountFile)
	{
		Id temp = 0;
		successorCountFile.seekp(0, fstream::end); // write to the end of the file
		successorCountFile.write((const char*)&temp, sizeof(Id));
		//successorCountFile.flush(); 
	}
	return numNodes++;
}

void DiskCDAGBuilder::printSuccessorCountFile()
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

// void updateSuccessorCountInFile(Id nodeId, Id incCount)
// {
// 	streampos pos(sizeof(Id)*nodeId);
// 	succsCountTempFile.seekg(pos, ios::beg);
// 	Id curVal = 0;
// 	succsCountTempFile.read((char*)&curVal, sizeof(Id));

// 	curVal += incCount;

// 	succsCountTempFile.seekg(pos, ios::beg);
// 	succsCountTempFile.write((const char*)&curVal, sizeof(Id));
// }

// void writeSuccessorInFile(int parentNodeId, int childNodeId)
// {
// 	// Start by getting the successor bitset for parent node
// 	streampos pos((numOfBytesForSuccBS*sizeof(BYTE)) * parentNodeId);
// 	succsListTempFile.seekg(pos, ios::beg);
// 	succsListTempFile.read((char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));

// 	// Update the bitset vector with the childNodeId
// 	utils::setBitInBitset(succsBitSet, childNodeId, numOfBytesForSuccBS);

// 	// Write back the updated successor list to the file
// 	succsListTempFile.seekp(pos, ios::beg);
// 	succsListTempFile.write((const char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));
// }

// void readSuccessorsFromFile(int nodeId, std::vector<Id> &succsList)
// {
// 	succsList.clear();
// 	// Get to the node in the file
// 	streampos pos((numOfBytesForSuccBS*sizeof(BYTE)) * nodeId);
// 	succsListTempFile.seekg(pos, ios::beg);

// 	// Read the successor bitset from the file
// 	succsListTempFile.read((char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));

// 	// fill in the result vector
// 	utils::getOnesPositionsInBitSet(succsBitSet, numOfBytesForSuccBS, succsList);
// }

}