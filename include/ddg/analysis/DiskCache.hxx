#include <fstream>
#include <iostream>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <climits>
#include <algorithm>
#include <deque>

#define DEBUG(msg) do { if (!DEBUG_ENABLED) {} \
                   else std::cout << __FILE__ << ":" << __LINE__ << " " << msg; \
               		} while(0)
#ifdef DEBUG_FLAG
#define DEBUG_ENABLED 1
#else
#define DEBUG_ENABLED 0
#endif

namespace ddg{

	using namespace std;
	using namespace llvm;

	template <typename Data, typename DataId>
	class DiskCache
	{
	public:
		DiskCache(int bs, int numSlots, bool flag = false): BLOCK_SIZE(bs),
									  NUM_SLOTS(numSlots),
									  writeBackFlag(flag),
									  initFlag(false),
									  dataCount(),
									  MAX_ITEMS_PER_SLOT(BLOCK_SIZE/sizeof(Data)),
									  MAX_ITEMS_IN_CACHE(MAX_ITEMS_PER_SLOT*NUM_SLOTS)
		{}

		~DiskCache()
		{
			// Invalidate all the slots
			for(int i=0; i<NUM_SLOTS; ++i)
			{
				invalidateSlot(i);
			}

			// clear up all the Data nodes
			while(!availableDataNodesQ.empty())
			{
				delete availableDataNodesQ.front();
				availableDataNodesQ.pop_front();
			}

			// Delete slots
			delete []slots;

			// Delete use list
			ListNode *curNode = useListHead;
			while(curNode)
			{
				ListNode *temp = curNode;
				curNode = curNode->next;
				delete temp;
			}
		}

		bool init(const string &file,					
					const string &indexFile ="",
					const unsigned int p=0)
		{
			initFlag = true;

			if(BLOCK_SIZE == 0 || BLOCK_SIZE < sizeof(Data) || NUM_SLOTS == 0)
			{
				initFlag = false;
				return initFlag;
			}

			policy = p;
			dataFileName = file;
			slots = new SLOT[NUM_SLOTS];

			// // Reserve memory for slots
			// for(int i=0; i<NUM_SLOTS; ++i)
			// {
			// 	slots[i].reserve(MAX_ITEMS_PER_SLOT+1);
			// }

			// // Pre-allocate Data and SlotIdSlotIndex objects
			// for(int i=0; i<MAX_ITEMS_IN_CACHE; ++i)
			// {
			// 	Data *node = new Data();
			// 	availableDataNodesQ.push_back(node);

			// 	SlotIdSlotIndex *obj = new SlotIdSlotIndex();
			// 	availableSlotIdIndexObjs.push_back(obj);
			// }

			// Initialize use list
			useListHead = new ListNode(0);
			ListNode *curNode = useListHead;
			slotIdToListNodeMap[0] = useListHead;
			for(int i=1; i<NUM_SLOTS; ++i)
			{
				ListNode *temp = new ListNode(i);
				temp->prev = curNode;
				curNode->next = temp;
				curNode = temp;
				slotIdToListNodeMap[i] = temp;
			}
			useListTail = curNode;

			// open handles to required files
			if(!initFileHandle(dataFileName, dataFileHandle, writeBackFlag)){
				cout << "\n Error in opening data file";
				initFlag = false;
			}
			if(!indexFile.empty() && !initFileHandle(indexFile, dataIndexFileHandle)){
				cout << "\n Error in opening index file";
				initFlag = false;
			}

			// If all is well till now, read in the index file
			if(initFlag){
				cout << "\n Indexing the graph file..." << flush;
				if(!indexFile.empty())
				{
					readDataIndexFile();
				}
				else
				{
					// no data index file specified
					bool temp = writeBackFlag;
					writeBackFlag = false;
					createDataIndex(false);
					writeBackFlag = temp;
				}
				cout << "done!" <<flush;
				dataCount = dataIdBlockRangeList.size() > 0 ? dataIdBlockRangeList[dataIdBlockRangeList.size()-1].end : 0;
			}

			return initFlag;
		}

		Id getDataCount()
		{
			return dataCount;
		}

		virtual void createDataIndex(bool dumpToFile)
		{
			DEBUG("\n Create index for the data file in cache.\n");
			// clear any flags set and reset the file pointer to beginning
			dataFileHandle.clear();
			dataFileHandle.seekg(0, ios::beg);

			// start creating the index
			streampos startPos = 0;
			while(dataFileHandle.peek() != EOF)
			{
				startPos = dataFileHandle.tellg();
				int slotId = getAvailableSlot();
				// Invalidate the current slot before reading
				invalidateSlot(slotId);
				readBlockInSlot(slotId, 0); // we don't care about the DataId being passed in
				int count = slots[slotId].size();
				if(count > 0)
				{
					DataIdRange range;
					range.setRange(slots[slotId][0]->getId(),
									slots[slotId][count-1]->getId(),
									startPos);
					dataIdBlockRangeList.push_back(range);
				}
			}

			sort(dataIdBlockRangeList.begin(), dataIdBlockRangeList.end());

			// Test code: Iterate over set to print block range
			if(DEBUG_ENABLED)
			{
				DEBUG("\n Printing block offset range");
				typename vector<DataIdRange>::iterator it = dataIdBlockRangeList.begin();
				for(; it != dataIdBlockRangeList.end(); ++it)
				{
					(*it).printRange();
				}
				DEBUG("\n Index created for thed data file\n");
			}
		}

		// Gets a read-only pointer to the data
		virtual Data* getData(const DataId id)
		{
			Data *retVal = 0;
			SlotIdSlotIndex* dataInfo = isInCache(id);
			int slotId = dataInfo ? dataInfo->slotId : -1;
			int slotIndex = dataInfo ? dataInfo->slotIndex : -1;
			dataInfo = 0;
			if(slotId < 0)
			{
				DEBUG("\n" << id <<" is not in cache");
				// Get a slot id to read a block into.
				slotId = getAvailableSlot();
				// Invalidate the current slot before reading
				invalidateSlot(slotId);

				// Find the block having the data and its 
				// seek position in the file.
				// Seek to the required block in the file.
				streampos off = getStreamOffsetForDataId(id);
				DEBUG("\n getData : " << id << ", seeking to " << off);
				seekForRead(dataFileHandle, off);
				DEBUG("\n and seeked to " << dataFileHandle.tellg());
				

				// Read the block in slot.
				slotIndex = readBlockInSlot(slotId, id);

			}
			else
			{
				DEBUG("\n" << id <<" is in cache at slot : " << slotId);
			}

			// The requested data should exists in cache.
			if(slotIndex == -1)
			{
				cout <<"\n FATAL CACHE ERROR : Sync error between cache data structures!";
				cout <<"\n Was trying to fetch data with id :" << id;
				cout <<flush;
				retVal = 0;
			}
			else
			{
				retVal = slots[slotId][slotIndex];
			}
			
			// Update the use list to mark this slot
			markSlotAsMRU(slotId);
			return retVal;
		}


		virtual int getAvailableSlot()
		{
			int retVal = -1;
			switch (policy)
			{
				case 0:
				default:
					retVal = applyLRUPolicy();
					break;
			}

			return retVal; // the default policy 
		}

	private:

		struct SlotIdSlotIndex
		{
			int slotId;
			int slotIndex;
			SlotIdSlotIndex(int s, int ind):slotId(s), slotIndex(ind)
			{}

			SlotIdSlotIndex():slotId(-1), slotIndex(-1)
			{}

			SlotIdSlotIndex& operator = (const SlotIdSlotIndex& rhs)
			{
				slotId = rhs.slotId;
				slotIndex = rhs.slotIndex;
				return *this;
			}
		};

		// This method assumes that dataFileHandle is pointing to 
		// appropriate block in the file. Will read from the current
		// seek position. Caller should seek into the appropriate location
		// in the file
		int readBlockInSlot(const unsigned int slotId, DataId id)
		{
			assert(slotId < NUM_SLOTS);
			DEBUG("\n readBlockInSlot "<<slotId);

			// Start reading from the file
			// Implementation Note :-
			// From the API perspective its good idea to stop reading 
			// when you reach the Block size and not when you reached 
			// the next block offset as specified by the data index 
			// data structure
			size_t curSize = 0;
			DataId begin = INT_MAX, end = 0;
			int slotIndex = -1;
			while(true)
			{
				Data* node = getAvailableDataNode();
				streampos beforeReadOff = dataFileHandle.tellg();
				//node->readNodeFromASCIIFile(dataFileHandle);
				node->readNodeFromBinaryFile(dataFileHandle);
				curSize += sizeof(*node);
				if(curSize < BLOCK_SIZE && dataFileHandle.tellg() != -1)
				{
					slots[slotId].push_back(node);
					SlotIdSlotIndex dataInfo(slotId, slots[slotId].size()-1);
					dataIdToSlotMap[node->getId()] = dataInfo;
					if(id == node->getId())
						slotIndex = slots[slotId].size()-1;
				}
				else
				{
					availableDataNodesQ.push_back(node);
					// we have read an extra data node.
					// Seek back by the size of the node read
					//dataFileHandle.seekg(beforeReadOff, ios::beg);
					if(dataFileHandle.eof())
					{
						// If we reached the eof then beforeReadOff is
						// set to -1 and the subsequent read would set
						// the fail bit. (TODO fix the subsquent read)
						// Go to the beginning of the file.
						//seekForRead(dataFileHandle, 0);

						// THERE IS SOMETHING FISHY HERE. COMMENTING IT FOR
						// NOW. 
					}
					else
					{
						seekForRead(dataFileHandle, beforeReadOff);
					}
					break;
				}
			}

			if(DEBUG_ENABLED)
			{
				DEBUG("\nreadBlockInSlot : done reading the slot. Dumping slot\n");
				dumpSlot(slotId);
			}
			return slotIndex;
		}

		void dumpSlot(int slotId)
		{
			if(slots[slotId].size() < 1)
			{
				cout << "Empty Slot";
				return;
			}
			SLOT_ITERATOR it = slots[slotId].begin();
			for(; it != slots[slotId].end(); ++it)
			{
				cout << (*it)->getId() <<" ";
			}
		}

		void invalidateSlot(const unsigned int slotId)
		{
			assert(slotId < NUM_SLOTS);

			if(writeBackFlag && slots[slotId].size() > 0)
			{
				DEBUG("\n Doing a write back for slot : "<<slotId);
				SLOT_ITERATOR it = slots[slotId].begin();
				Id firstEleId = (*it)->getId();
				seekForWrite(dataFileHandle, getStreamOffsetForDataId(firstEleId));
				DEBUG(" with tellp() value at : " << dataFileHandle.tellp());
				for(; it != slots[slotId].end(); ++it)
				{
					(*it)->writeToStream(dataFileHandle);
					//(*it)->print(cout);
				}
				dataFileHandle.flush();
			}

			// move all the used data nodes to available queue
			// and then empty the dictionary container
			SLOT_ITERATOR it = slots[slotId].begin();
			for(; it != slots[slotId].end(); ++it)
			{
				dataIdToSlotMap.erase((*it)->getId());
				availableDataNodesQ.push_back(*it);
			}

			slots[slotId].clear();
		}

		Data* getAvailableDataNode()
		{
			Data *retVal = 0;
			if(!availableDataNodesQ.empty())
			{
				retVal = availableDataNodesQ.front();
				availableDataNodesQ.pop_front();
				retVal->reset();
			}
			else
			{
				cout << "\n Warning : Allocating a new Data node!";
				cout << " Shouldn't happen if the nodes were pre-allocated";
				retVal = new Data();
			}
			return retVal;
		}

		SlotIdSlotIndex* isInCache(const DataId id)
		{
			SlotIdSlotIndex *dataInfo = 0;
			DATA_TO_SLOT_MAP_ITERATOR it = dataIdToSlotMap.find(id);
			if(it != dataIdToSlotMap.end())
			{
				dataInfo = &(it->second);
			}
			return dataInfo;
		}

		int applyLRUPolicy()
		{
			// the head of the use list marks the least recently used
			// slot. Return the id and subsequent call to markSlotAsMRU
			// will move this list node to the tail.
			return useListHead->slotId;
		}

		void markSlotAsMRU(const int slotId)
		{
			ListNode *curNode = slotIdToListNodeMap[slotId];
			
			// If its already at the tail then we can simply return
			if(curNode == useListTail)
				return;

			// Else move this list node to the tail
			if(curNode == useListHead)
			{
				curNode->next->prev = 0;
				useListHead = curNode->next;
			}
			else
			{
				// its a middle node
				curNode->prev->next = curNode->next;
				curNode->next->prev = curNode->prev;
			}
			curNode->prev = useListTail;
			useListTail->next = curNode;
			curNode->next = 0;

			useListTail = curNode;
		}

		bool initFileHandle(const string &name, fstream &handle, bool rwMode=false)
		{
			bool retVal = true;
			if(name.empty())
			{
				retVal = false;
			}
			if(rwMode)
			{
				handle.open(name.c_str(), fstream::in|fstream::out);
			}
			else
			{
				handle.open(name.c_str(), fstream::in);
			}

			if(!handle)
				retVal = false;
			return retVal;
		}

		void readDataIndexFile()
		{
			//cout << "\n In disk cache readDataIndexFile \n";
			int i=0;
			streampos pos(0);
			streampos prev(0);
			DataId begin = i;
			while(dataIndexFileHandle.read((char*)&pos, sizeof(streampos)).good())
			{				
				//cout << (long)pos <<" ";
				if(pos != prev)
				{
					DataIdRange range;
					range.setRange(begin, i-1, prev);
					dataIdBlockRangeList.push_back(range);
					begin = i;
					prev = pos;
				}
				pos = 0;
				++i;
			}

			DataIdRange range;
			range.setRange(begin, i-1, prev);
			dataIdBlockRangeList.push_back(range);
			sort(dataIdBlockRangeList.begin(), dataIdBlockRangeList.end());

			// Test code: Iterate over set to print block range
			if(DEBUG_ENABLED)
			{
				DEBUG("\n Priting block offset range");
				typename vector<DataIdRange>::iterator it = dataIdBlockRangeList.begin();
				for(; it != dataIdBlockRangeList.end(); ++it)
				{
					(*it).printRange();
				}
			}
		}

		streampos getStreamOffsetForDataId(DataId &id)
		{
			int start = 0, end = dataIdBlockRangeList.size()-1, mid;
			streampos retVal = -1;
			while(start <= end)
			{
				mid  = (start + end) >> 1;
				if(dataIdBlockRangeList[mid].isInRange(id))
				{
					retVal = dataIdBlockRangeList[mid].pos;
					break;
				}
				else if(id < dataIdBlockRangeList[mid].begin)
				{
					end = mid-1;
				}
				else
				{
					start = mid+1;
				}
			}
			return retVal;
		}

		void seekForRead(fstream &handle, streampos offset)
		{
			// if(handle.is_open())
			// {
			// 	cout <<"\n In seekForRead - Handle is open."	;
			// }
			// if(handle.fail())
			// {
			// 	cout <<"\n In seekForRead - Fail bit set";
			// }
			// if(handle.bad())
			// {
			// 	cout <<"\n In seekForRead - bad bit set";
			// }
			// if(handle.good())
			// {
			// 	cout <<"\n In seekForRead - Good bit set";
			// }
			// if(handle.eof())
			// {
			// 	cout << "\n In seekForRead - Eof bit set";
			// }
			handle.seekg(offset, ios::beg);
			if(handle.tellg() == -1)
			{
				if(handle.eof() || handle.fail())
				{
					handle.clear();
					handle.seekg(offset, ios::beg); //reset to beginning
					DEBUG("\nReached end of file - reset to beginning");
				}
				else
				{
					// handle.seekg(0, ios::beg); //reset to beginning
					cout << "\n Cache error in stream associated with file :" << dataFileName;
				}
			}
		}

		void seekForWrite(fstream &handle, streampos offset)
		{
			// if(handle.is_open())
			// {
			// 	cout <<"\n In seekForWrite - Handle is open."	;
			// }
			// if(handle.fail())
			// {
			// 	cout <<"\n In seekForWrite - Fail bit set";
			// }
			// if(handle.bad())
			// {
			// 	cout <<"\n In seekForWrite - bad bit set";
			// }
			// if(handle.good())
			// {
			// 	cout <<"\n In seekForWrite - Good bit set";
			// }
			// if(handle.eof())
			// {
			// 	cout << "\n In seekForWrite - Eof bit set";
			// }
			handle.seekp(offset, ios::beg);
			if(handle.tellp() == -1)
			{
				if(handle.eof() || handle.fail())
				{
					handle.clear();
					handle.seekp(offset, ios::beg); //reset to beginning
					DEBUG("\n Reached end of file - reset to beginning");
				}
				else
				{
					// handle.seekg(0, ios::beg); //reset to beginning
					cout << "\n Cache error in stream associated with file :" << dataFileName;
				}
			}
		}

		// Member variables and data structures
		struct ListNode
		{
			ListNode *prev;
			ListNode *next;
			int slotId;
			ListNode(int id):slotId(id),
							 prev(0),
							 next(0)
			{}
		};

		struct DataIdRange{
		 	DataId begin;
		 	DataId end;
		 	int slotId;
		 	streampos pos;

		 	DataIdRange():begin(-1), end(-1), pos(-1), slotId(-1){}

		 	bool operator< (const DataIdRange& other) const
		 	{
		 		return this->end < other.end;
		 	}

		 	bool isInRange(DataId id)
		 	{
		 		return (begin <= id && id <= end);
		 	}

		 	void setRange(DataId b, DataId e, int slot)
		 	{
		 		begin = b; end = e; slotId = slot;
		 	}

		 	void setRange(DataId b, DataId e, streampos p)
		 	{
		 		begin = b; end = e; pos = p;
		 	}

		 	void printRange() const
		 	{
		 		cout << "\n" << begin << " - " <<end << " : " <<pos;
		 	}
		};

		typedef typename std::vector<Data*> SLOT;
		typedef typename std::vector<Data*>::iterator SLOT_ITERATOR;
		typedef typename std::map<DataId, SlotIdSlotIndex> DATA_TO_SLOT_MAP;
		typedef typename std::map<DataId, SlotIdSlotIndex>::iterator DATA_TO_SLOT_MAP_ITERATOR;

		const size_t BLOCK_SIZE;
		const int NUM_SLOTS;
		string dataFileName;
		bool initFlag;
		unsigned int policy;
		bool writeBackFlag;
		const unsigned int MAX_ITEMS_PER_SLOT;
		const unsigned int MAX_ITEMS_IN_CACHE;
		DataId dataCount;

		SLOT *slots;
		DATA_TO_SLOT_MAP dataIdToSlotMap;	

		ListNode *useListHead; // represents the LRU slot
		ListNode *useListTail; // represents the MRU slot
		map<int, ListNode*>  slotIdToListNodeMap; // map for a quick markSlotAsMRU() implementation

		fstream dataFileHandle;
		fstream dataIndexFileHandle;

		vector<DataIdRange> dataIdBlockRangeList;

		deque<Data*> availableDataNodesQ;
		deque<SlotIdSlotIndex*> availableSlotIdIndexObjs;


	public:
		// test methods
		void testCacheLRUPolicy()
		{
			cout << "\n Testing LRU policy";
			cout << "\n Serial testing for available slots";
			for(int i =0 ; i<2*NUM_SLOTS; ++i)
			{
				int slot = getAvailableSlot();
				cout << "\n Slot " << slot << " used.";
				markSlotAsMRU(slot);
				cout << "\n Slot " << slot << " marked as used.";
			}
			cout << "\n Random testing for available slots";
			cout << "\n Marking random slots as used";
			for(int i =0 ; i<2*NUM_SLOTS; ++i)
			{
				int slot = rand() % NUM_SLOTS;
				markSlotAsMRU(slot);
				cout << "\n Slot " << slot << " marked as used.";
			}
			cout << "\n Next available slot is " << getAvailableSlot();
		}

		void testGetData(int numNodes)
		{
			Id n = 0;
			const Data* node = 0;
			cout << "\n Get Node " << n;
			node = getData(n);
			if(node)
			{
				node->print(cout);
			}
			else
			{
				cout << "\n Null node for " << n;
			}

			n = 1;
			node = getData(n);
			if(node)
			{
				node->print(cout);
			}
			else
			{
				cout << "\n Null node for " << n;
			}

		}

		void testReadOfDiskGraph(int numNodes)
		{
			ofstream out("printofdiskgraph");
			for(int i=0; i<numNodes; ++i)
			{
				const Data *node = getData(i);
				if(node)
				{
					node->print(out);
				}
				else
				{
					cout << "\n Null node for " << i;
				}
			}
			out.close();
		}
		
	};
}

