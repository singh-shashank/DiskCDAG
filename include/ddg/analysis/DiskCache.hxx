#include <fstream>
#include <iostream>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <climits>

namespace ddg{

	using namespace std;
	using namespace llvm;

	template <typename Data, typename DataId>
	class DiskCache
	{
	public:
		DiskCache(int bs, int numSlots): BLOCK_SIZE(bs),
									  NUM_SLOTS(numSlots),
									  initFlag(false)
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
				availableDataNodesQ.pop();
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

			// Close file handles
			dataFileHandle.close();
			dataIndexFileHandle.close();
		}

		bool init(const string file, 
					const string indexFile,
					const unsigned int p=0)
		{
			initFlag = true;

			if(BLOCK_SIZE == 0 || BLOCK_SIZE < sizeof(Data) || NUM_SLOTS == 0)
			{
				initFlag = false;
				return initFlag;
			}

			policy = p;
			filename = file;
			slots = new SLOT[NUM_SLOTS];

			// Setup slotid to range map
			DataIdRange range;
			for(int i=0; i<NUM_SLOTS;++i)
			{
				slotIdToDataIdRangeMap[i] = range;
			}

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
			if(!initFileHandle(file, dataFileHandle)){
				cout << "\n Error in opening data file";
				initFlag = false;
			}
			if(!initFileHandle(indexFile, dataIndexFileHandle)){
				cout << "\n Error in opening index file";
				initFlag = false;
			}

			// If all is well till now, read in the index file
			if(initFlag){
				readDataIndexFile();
			}

			return initFlag;
		}

		// Gets a read-only pointer to the data
		const Data* getData(const DataId id)
		{
			Data *retVal = 0;
			int slotId = isInCache(id);
			if(slotId < 0)
			{
				cout << "\n" << id <<" is not in cache";
				// Get a slot id to read a block into.
				slotId = getAvailableSlot();

				// Find the block having the data and its 
				// seek position in the file.
				// Seek to the required block in the file.
				cout << "\n getData : " << id << ", seeking to " << dataIdToBlockMap[id];
				dataFileHandle.seekg(dataIdToBlockMap[id], ios::beg);
				cout << "\n and seeked to " << dataFileHandle.tellg();

				// Read the block in slot.
				readBlockInSlot(slotId);

			}
			else
			{
				cout << "\n" << id <<" is in cache at slot : " << slotId;
			}

			// The requested data exists in cache.
			SLOT_ITERATOR it = slots[slotId].find(id);
			if(it == slots[slotId].end())
			{
				cout <<"\n FATAL CACHE ERROR : Sync error between cache data structures!";
				cout <<"\n Was trying to fetch data :" << id;
				exit(-1);
			}
			else
			{
				retVal = it->second;
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


	private:
		// This method assumes that dataFileHandle is pointing to 
		// appropriate block in the file. Will read from the current
		// seek position. Caller should seek into the appropriate location
		// in the file
		void readBlockInSlot(const unsigned int slotId)
		{
			assert(slotId < NUM_SLOTS);
			cout <<"\n readBlockInSlot "<<slotId;

			// Invalidate the current slot before reading
			invalidateSlot(slotId);

			// Start reading from the file
			// Implementation Note :-
			// From the API perspective its good idea to stop reading 
			// when you reach the Block size and not when you reached 
			// the next block offset as specified by the data index 
			// data structure
			size_t curSize = 0;
			DataId begin = INT_MAX, end = 0;
			while(true)
			{
				Data* node = getAvailableDataNode();
				node->readNodeFromASCIIFile(dataFileHandle);
				curSize += sizeof(*node);
				if(curSize < BLOCK_SIZE)
				{
					slots[slotId][node->dynId] = node;
					begin = min(begin, node->dynId);
					end = max(end, node->dynId);
				}
				else
				{
					break;
				}
			}

			slotIdToDataIdRangeMap[slotId].setRange(begin, end, slotId);

			cout <<"\n readBlockInSlot : done reading the slot. Dumping slot\n";
			dumpSlot(slotId);
		}

		void dumpSlot(int slotId)
		{
			SLOT_ITERATOR it = slots[slotId].begin();
			for(; it != slots[slotId].end(); ++it)
			{
				cout << it->first <<" ";
			}
		}

		void invalidateSlot(const unsigned int slotId)
		{
			assert(slotId < NUM_SLOTS);

			// move all the used data nodes to available queue
			// and then empty the dictionary container
			SLOT_ITERATOR it = slots[slotId].begin();
			for(; it != slots[slotId].end(); ++it)
			{
				availableDataNodesQ.push(it->second);
			}

			slots[slotId].clear();
		}

		Data* getAvailableDataNode()
		{
			Data *retVal = 0;
			if(!availableDataNodesQ.empty())
			{
				retVal = availableDataNodesQ.front();
				availableDataNodesQ.pop();
				retVal->reset();
			}
			else
			{
				retVal = new Data();
			}
			return retVal;
		}

		// TODO : optimize the implementation from O(n) to O(log(n))
		int isInCache(const DataId id)
		{
			int slot = -1;
			SLOT_TO_RANGE_MAP_ITERATOR it = slotIdToDataIdRangeMap.begin();
			for(; it != slotIdToDataIdRangeMap.end(); ++it)
			{
				if(it->second.isInRange(id))
				{
					slot = it->second.slotId;
					break;
				}
			}
			return slot;
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

		bool initFileHandle(const string &name, ifstream &handle)
		{
			bool retVal = true;
			if(name.empty())
			{
				retVal = false;
			}
			handle.open(name.c_str());
			if(!handle)
				retVal = false;
			return retVal;
		}

		void readDataIndexFile()
		{
			cout << "\n In disk cache readDataIndexFile \n";
			int i=0;
			streampos pos(0);
			while(dataIndexFileHandle.read((char*)&pos, sizeof(streampos)).good())
			{				
				dataIdToBlockMap[i] = pos;
				++i;
				cout << (long)pos <<" ";
				pos = 0;
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

		 	DataIdRange():begin(-1), end(-1){}

		 	bool operator< (const DataIdRange& other) const
		 	{
		 		return this.end < other.end;
		 	}

		 	bool isInRange(DataId id)
		 	{
		 		return (begin <= id && id <= end);
		 	}

		 	void setRange(DataId b, DataId e, int slot)
		 	{
		 		begin = b; end = e; slotId = slot;
		 	}

		 	//bool operator() (const DataIdRange& other) const
		};
		typedef typename std::map<DataId, Data*> SLOT;
		typedef typename std::map<DataId, Data*>::iterator SLOT_ITERATOR;
		typedef typename std::map<int, DataIdRange> SLOT_TO_RANGE_MAP;
		typedef typename std::map<int, DataIdRange>::iterator SLOT_TO_RANGE_MAP_ITERATOR;

		const size_t BLOCK_SIZE;
		const int NUM_SLOTS;
		string filename;
		bool initFlag;
		unsigned int policy;

		SLOT *slots;
		SLOT_TO_RANGE_MAP slotIdToDataIdRangeMap;	

		ListNode *useListHead; // represents the LRU slot
		ListNode *useListTail; // represents the MRU slot
		map<int, ListNode*>  slotIdToListNodeMap; // map for a quick markSlotAsMRU() implementation

		ifstream dataFileHandle;
		ifstream dataIndexFileHandle;

		map<DataId, streampos> dataIdToBlockMap;

		queue<Data*> availableDataNodesQ;

		
	};
}

