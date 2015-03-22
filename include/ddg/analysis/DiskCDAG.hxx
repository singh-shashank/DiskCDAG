#ifndef DiskCDAG_HXX
#define DiskCDAG_HXX

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
#include <sstream>
#include <istream>

#include "ddg/analysis/DiskCache.hxx"

namespace ddg
{
using namespace llvm;
using namespace std;

#ifdef FULL_DAG
typedef size_t payload_type;
#else
typedef std::set<size_t> payload_type;
#endif

typedef unsigned char BYTE;

class DiskCDAGBuilder;
class DiskCDAG;

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

void dumpBitSet(BYTE *bitSet, int numSets)
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
	BYTE byteMask = (1 << 7) >> bitIndex;
	bitSet[groupIndex] = bitSet[groupIndex] | byteMask;
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

class DiskCDAGBuilder: public LazyGraphVisitor<payload_type>
{
	protected:
		DiskCDAG *cdag;
		map<Address, size_t> loadMap; //Maps the memory address to ddg input node id

	public:
		DiskCDAGBuilder(DiskCDAG *cdag) :	cdag(cdag), loadMap() 
		{

		}

		virtual void visitNode(LazyNode<payload_type> *node, Predecessors	&predecessors, Operands &operands);
};

class DiskCDAG
{
	private:
		struct CDAGNode{
			Id dynId;	// Dynamic Id 
			Address addr;	// Addresses represented by this node if any
			unsigned int type;	// LLVM Type of the node
			Id staticId;
			size_t blockId;
			std::vector<Id> predsList;	// Vector containing list of predecessors
			std::vector<Id> succsList;	// Vector containing list of successors

			// TODO : initialize it with proper values
			CDAGNode(): dynId(0),
						addr(0),
						type(0),
						staticId(0),
						blockId(0)
			{
			}

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
				out << "\n Block Id : " << blockId;
				out << "\n------------------------------------------------------\n";
			}

			// TODO : try taking in a output stream directly
			// 		  instead of returning a string
			void getStringToWriteNode(stringstream &ss)
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

			bool readNodeFromASCIIFile(istream &file)
			{
				std::string temp, line;
				

				// Read dynId, static Id, type and addr
				{
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
				blockId = 0;
				staticId = 0;
				addr = 0;
				type = 0;
				predsList.clear();
				succsList.clear();
			}
		};

		static const size_t CDAGNODE_SIZE = sizeof(CDAGNode);

		BYTE *succsBitSet;
		size_t numOfBytesForSuccBS;


		size_t blockSize;
		size_t blockCount;
		size_t curBlockSize;

		std::map<Id, CDAGNode*> idToCDAGNodeMap;

		fstream graphDumpFile;
		fstream succsListTempFile;

		// TODO : Temp ofstreams - remove these!
		ofstream printBeforeWriteFile;
		ofstream printAfterReadFile;

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

			numOfBytesForSuccBS = convertNumNodesToBytes(count);
			cout << "\ncount :" <<count;
			cout << "\n numOfBytesForSuccBS: " <<numOfBytesForSuccBS;
			succsBitSet = new BYTE[numOfBytesForSuccBS];
			memset(succsBitSet, 0, numOfBytesForSuccBS);

			cout << "\nSize of succsBitSet " << sizeof(BYTE);
			cout << "\nSize of succsBitSet " << sizeof(succsBitSet);
			cout << "\nSize of *succsBitSet " << sizeof(*succsBitSet);

			// TODO : compare with CDAGNODE_WITHSUCC_SIZE
			if(blockSize != 0 && blockSize < CDAGNODE_SIZE)
				cout << "\n Block size is less than CDAG node size..Aborting!";
			else if(blockSize == 0){
				cout << "\n Block size is passed as zero - memory based graph will be generated.";
			}


			//  TODO : revisit this part for checking corner cases and add some comments
			graphDumpFile.open("graphdump", std::fstream::out | std::fstream::trunc);
			graphDumpFile.close();
			graphDumpFile.open("graphdump", std::fstream::in | std::fstream::out);
			
			succsListTempFile.open("succslisttemp", std::fstream::out | std::fstream::trunc);
			succsListTempFile.close();
			succsListTempFile.open("succslisttemp", std::fstream::in | std::fstream::out | std::fstream::binary);
			
			printBeforeWriteFile.open("printbeforewrite");
			printAfterReadFile.open("printafterread");

			// dumping blocks of memory to the file
			for(int i=0; i < count; ++i)
			{
				succsListTempFile.write((const char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));
			}

			//testSuccessorsReadWrite();
		}

		void testSuccessorsReadWrite()
		{
			writeSuccessorInFile(1, 101);
			writeSuccessorInFile(1, 109);
			writeSuccessorInFile(1, 117);
			writeSuccessorInFile(1, 124);
			writeSuccessorInFile(1, 200);

			cout <<"\n Printing succs list in testSuccessorsReadWrite()\n";
			std::vector<Id> succsList;
			readSuccessorsFromFile(1, succsList);
			for(std::vector<Id>::iterator it = succsList.begin();
				it != succsList.end(); ++it)
			{
				cout << *it << " ";
			}
			cout <<"\n";
		}

		//Adds the successor 'scsrId' to node 'nodeId'
		void addSuccessor(size_t nodeId, size_t scsrId)
		{
			scsrList[nodeId][scsrCnt[nodeId]++] = scsrId;
		}

	public:
		DiskCDAG(size_t count) : numNodes(0),
								 count(count),
								 blockSize(0),
								 blockCount(0),
								 curBlockSize(0)
		{
			init(count);
		}

		DiskCDAG(Ids &ids, size_t bs) : numNodes(0),
									 	blockSize(bs),
									 	blockCount(0),
									 	curBlockSize(0)
		{
			cout << "\n Size of CDAGNode " << sizeof(CDAGNode);
			CDAGCounter counter(ids); //Get the count of expected no. of nodes
			count = counter.getCount();
			init(count);
		}

		void testMethodForDiskCache()
		{
			// DiskCache<CDAGNode, Id> cache(512,9);
			// cache.init("diskgraph", "diskgraph_index" );
			// cache.testCacheLRUPolicy();

			DiskCache<CDAGNode, Id> cache(512,4);
			cache.init("diskgraph", "diskgraph_index" );
			cache.testReadOfDiskGraph(numNodes);
			//cache.testGetData(numNodes);
		}

		~DiskCDAG()
		{
			// // Check successor implementation
			// ofstream t("test_succ");
			// t << "\n Printing successors : \n";
			// for(int i=0; i<numNodes; ++i)
			// {
			// 	t << i << " : ";
			// 	vector<Id> temp;
			// 	readSuccessorsFromFile(i, temp);
			// 	for(vector<Id>::iterator it = temp.begin();
			// 		it != temp.end(); ++it)
			// 	{
			// 		t<< *it << " ";
			// 	}
			// 	t << "\n---------------------------------\n";
			// }
			// t <<"\n";
			// t.close();

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

			delete []succsBitSet;

			// delete the graph nodes and clean up the maps
			std::map<Id, CDAGNode*>::iterator it1 = idToCDAGNodeMap.begin();
			for(; it1 != idToCDAGNodeMap.end(); ++it1)
			{
				delete it1->second;
			}
			idToCDAGNodeMap.clear();

			// close the files
			if(graphDumpFile)
			{
				graphDumpFile.close();
			}
			if(printBeforeWriteFile)
			{
				printBeforeWriteFile.close();
			}
			if(printAfterReadFile)
			{
				printAfterReadFile.close();
			}
			if(succsListTempFile)
			{
				succsListTempFile.close();
				// /remove(succsListTempFile);
			}
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
			type[numNodes] = currType;
			staticId[numNodes] = id;

			CDAGNode *node = new CDAGNode();
			node->dynId = numNodes;
			node->type = currType;
			node->staticId = id;
			if(currType == Instruction::Load){
				// addr is only consumed in case of load instruction
				node->addr = addr;
			}

			addUpdateNodeToBlock(node);

			//node->blockId = blockCount; // this will be updated by addUpdateNodeToBlock
			//idToCDAGNodeMap[node->dynId] = node;

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
				idToCDAGNodeMap[nodeId]->predsList.push_back(*it);
				writeSuccessorInFile((*it), nodeId);

				list[i] = *it;
				++it;
			}

			addUpdateNodeToBlock(idToCDAGNodeMap[nodeId]);
			
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

		void writeSuccessorInFile(int parentNodeId, int childNodeId)
		{
			// Start by getting the successor bitset for parent node
			streampos pos((numOfBytesForSuccBS*sizeof(BYTE)) * parentNodeId);
			succsListTempFile.seekg(pos, ios::beg);
			succsListTempFile.read((char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));

			// Update the bitset vector with the childNodeId
			setBitInBitset(succsBitSet, childNodeId, numOfBytesForSuccBS);

			// Write back the updated successor list to the file
			succsListTempFile.seekp(pos, ios::beg);
			succsListTempFile.write((const char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));
		}

		void readSuccessorsFromFile(int nodeId, std::vector<Id> &succsList)
		{
			succsList.clear();
			// Get to the node in the file
			streampos pos((numOfBytesForSuccBS*sizeof(BYTE)) * nodeId);
			succsListTempFile.seekg(pos, ios::beg);

			// Read the successor bitset from the file
			succsListTempFile.read((char*)&succsBitSet[0], numOfBytesForSuccBS*sizeof(BYTE));

			// fill in the result vector
			getOnesPositionsInBitSet(succsBitSet, numOfBytesForSuccBS, succsList);
		}

		void updateGraphWithSuccessorInfo(size_t bs)
		{
			ofstream diskGraph("diskgraph");
			ofstream diskGraphIndex("diskgraph_index", ios::binary);


			streampos pos(0);
			size_t curBlockSize = 0;

			graphDumpFile.clear();
	 		graphDumpFile.seekg(0, ios::beg);
	 		blockCount = 0;

			while(!readBlockFromFile(graphDumpFile))
			{
				writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex, 
					bs, pos, curBlockSize);
				++blockCount;
			}

			writeGraphWithSuccessorInfoToFile(diskGraph, diskGraphIndex,
			 bs, pos, curBlockSize);
			diskGraph.close();
			diskGraphIndex.close();
		}

		void writeGraphWithSuccessorInfoToFile(ofstream &diskGraph,
											   ofstream &diskGraphIndex,
											   size_t bs,
											   streampos &pos,
											   size_t &curBlockSize)
		{
			
			vector<CDAGNode*> nodesToWriteList;
			vector<Id> succList;
			stringstream ss;

			map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
			for(; it != idToCDAGNodeMap.end(); ++it)
			{
				readSuccessorsFromFile(it->first, succList);
				CDAGNode *curNode = it->second;
				if(succList.size() > 0)
				{
					curNode->succsList = succList;
				}

				// Check if we reached the write block size
				size_t curNodeSize = sizeof(*curNode);
				if(curBlockSize + curNodeSize > bs)
				{
					// dump out nodesToWriteList
					vector<CDAGNode*>::iterator it1 = nodesToWriteList.begin();
					for(; it1 != nodesToWriteList.end(); ++it1)
					{
						(*it1)->getStringToWriteNode(ss);
						diskGraph << ss.str();
						diskGraphIndex.write((const char*)&pos, sizeof(streampos));
					}
					cout << pos << " ";
					pos = diskGraph.tellp();
					
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
				vector<CDAGNode*>::iterator it1 = nodesToWriteList.begin();
				for(; it1 != nodesToWriteList.end(); ++it1)
				{
					(*it1)->getStringToWriteNode(ss);
					diskGraph << ss.str();
					diskGraphIndex.write((const char*)&pos, sizeof(streampos));
				}
			}
		}

		//Generates the DiskCDAG using DiskCDAGBuilder
		static DiskCDAG* generateGraph(Ids& ids, int block_size)
		{
			DiskCDAG *cdag = new DiskCDAG(ids, block_size);
			DiskCDAGBuilder builder(cdag);

			builder.visit(ids);

			//Populate the successor list. Successor count has been already set by setPredecessor()
			cdag->setSuccessors();

			return cdag;
		}

		//Generates the DiskCDAG using user-specified 'Builder'
		template <typename Builder>
			static DiskCDAG* generateGraph(Ids& ids, int block_size)
			{
				DiskCDAG *cdag = new DiskCDAG(ids, block_size);
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
		void printGraph(ofstream& out)
		{
			out << numNodes << '\n';
			for(size_t i=0; i<numNodes; ++i)
			{
				out << i << ". Instruction: " << llvm::Instruction::getOpcodeName(type[i]) << "; StaticID: " << staticId[i] << "\n";
				size_t numPreds = predCnt[i];
				out << "num predecessors: " << numPreds << "\n";
				for(size_t j=0; j<numPreds; j++)
				{
					out << predList[i][j] << '\t';
				}
				out << '\n';
				size_t numScsrs = scsrCnt[i];
				out << "num successors: " << numScsrs << '\n';
				for(size_t j=0; j<numScsrs; j++)
				{
					out << scsrList[i][j] << '\t';
				}
				out << "\n------------------------------------------------------\n";
			}
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

		// Disk CDAG block management

		void flushCurrentBlockToDisk(bool flag)
		{
			printDiskGraph(printBeforeWriteFile);

			std::map<Id, CDAGNode*>::iterator it = idToCDAGNodeMap.begin();
	 		stringstream ss;
	 		for(; it!=idToCDAGNodeMap.end(); ++it)
	 		{
	 			it->second->getStringToWriteNode(ss);
	 			graphDumpFile << ss.str();
	 		}


	 		// TODO : test code for checking read of blocks - to remove
	 		if(flag)
	 		{
	 			// last flush to disk 
	 			graphDumpFile.clear();
	 			graphDumpFile.seekg(0, ios::beg);
	 			blockCount = 0;
	 			while(!readBlockFromFile(graphDumpFile))
	 			{
	 				printDiskGraph(printAfterReadFile);
	 				++blockCount;
	 			}
	 			printDiskGraph(printAfterReadFile);
	 		}
		}

		// Reads a block from a given stream to the 
		// in memory map
		bool readBlockFromFile(istream &file)
		{
			bool err = true;
			resetGraphState();

			streampos pos;
			while(!file.eof())
			{
				pos = file.tellg();
				CDAGNode *node = new CDAGNode();
				err = node->readNodeFromASCIIFile(file);
				curBlockSize += sizeof(*node);
				node->blockId = blockCount;
				if(curBlockSize < blockSize)
				{
					idToCDAGNodeMap[node->dynId] = node;
				}
				else
				{
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

				// reset all the required variables after flushing to
				// disk
				resetGraphState();

				// start using a new block now
				++blockCount;

				// update curBlockSize
				curBlockSize = sizeof(*node);
			}

			node->blockId = blockCount;
			idToCDAGNodeMap[nodeId] =  node;

			return blockCount;
		}


		void resetGraphState()
		{
			idToCDAGNodeMap.clear();
			curBlockSize = 0;

			// TODO : should delete all the CDAGNodes in map here
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
				size_t inputNodeId = cdag->addNode(Instruction::Load, 
					cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
					addr);
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
	size_t nodeId = cdag->addNode(instr->getOpcode(),
		cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
		0);
	for(Predecessors::iterator pred = predecessors.begin(), predEnd =	predecessors.end(); pred != predEnd;	++pred)
	{
		payload_type pred_data = (*pred)->get();
		predSet.insert(pred_data);
	}
	cdag->setPredecessor(nodeId, predSet);
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
				size_t inputNodeId = cdag->addNode(Instruction::Load, 
					cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
					addr);
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
		size_t nodeId = cdag->addNode(instr->getOpcode(), 
			cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
			0);
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