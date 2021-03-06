#include <fstream>
#include <iostream>

#include <string>
#include <vector>
#include <climits>
#include <algorithm>
#include <deque>

#include <boost/unordered_map.hpp>

#define DEBUG(msg) do { if (!DEBUG_ENABLED) {} \
                   else std::cout << __FILE__ << ":" << __LINE__ << " " << msg; \
               		} while(0)
#ifdef DEBUG_FLAG
#define DEBUG_ENABLED 1
#else
#define DEBUG_ENABLED 0
#endif

//#define DEBUG_ENABLED 1

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
									  inMemoryCache(false)
		{
			Data temp;
			MAX_ITEMS_PER_SLOT = BLOCK_SIZE/temp.getSize();
			MAX_ITEMS_IN_CACHE = MAX_ITEMS_PER_SLOT*NUM_SLOTS;
			// Its being set to TRUE for know but make it
			// configurable if the need be.
			// Just remember, when dealing with huge graphs
			// pre-allocation gives a huge gain i.e. reducing
			// analysis time from 24 hrs to just an hour or less.
			preAllocateFlag = true;
		}

		~DiskCache()
		{
			// Invalidate all the slots
			for(int i=0; i<NUM_SLOTS; ++i)
			{
				invalidateSlot(i);
			}

			while(!availableDataNodesQ->empty())
			{
				delete availableDataNodesQ->getItemFromPool();
			}
			while(!availableSlotIdIndexObjsQ->empty())
			{
				delete availableSlotIdIndexObjsQ->getItemFromPool();
			}
			delete availableDataNodesQ;
			delete availableSlotIdIndexObjsQ;

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

			if(BLOCK_SIZE < sizeof(Data) || NUM_SLOTS == 0)
			{
				initFlag = false;
				return initFlag;
			}
			if(BLOCK_SIZE == 0)
			{
				// We are maintaining an in memory cache.
				// Reset NUM_SLOTS to 1 i.e. the user passed
				// value will not matter.
				inMemoryCache = true;
				NUM_SLOTS = 1; 
			}

			policy = p;
			dataFileName = file;
			slots = new SLOT[NUM_SLOTS];

			// Initialize use list
			slotIdToListNodeMap.reserve(NUM_SLOTS);
			useListHead = new ListNode(0);
			ListNode *curNode = useListHead;
			//slotIdToListNodeMap[0] = useListHead;
			slotIdToListNodeMap.push_back(useListHead);
			for(int i=1; i<NUM_SLOTS; ++i)
			{
				ListNode *temp = new ListNode(i);
				temp->prev = curNode;
				curNode->next = temp;
				curNode = temp;
				//slotIdToListNodeMap[i] = temp;
				slotIdToListNodeMap.push_back(temp);
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
				cout << "\n Indexing the data file..." << flush;
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
				dataCount = dataIdBlockRangeList.size() > 0 ? dataIdBlockRangeList[dataIdBlockRangeList.size()-1].end : 0;
				cout << "done!";
			}

			// We have a valid data count now
			DEBUG("\nData count for " << dataFileName << ": " << dataCount);
			unsigned int maxDataObjectsRequired = MAX_ITEMS_IN_CACHE+1;
			if(dataCount > MAX_ITEMS_IN_CACHE)
			{
				// Reserve memory for slots
				for(int i=0; i<NUM_SLOTS; ++i)
				{
					slots[i].reserve(MAX_ITEMS_PER_SLOT+1);
				}
			}
			else
			{
				// We have enough space in cache to avoid data eviction.
				// Just pre-allocate Data and SlotIdSlotIndex objects
				maxDataObjectsRequired = dataCount + 10;

			}

			if(!preAllocateFlag)
			{
				availableDataNodesQ = new CircularQ<Data*>();
				availableSlotIdIndexObjsQ = new CircularQ<SlotIdSlotIndex*>();
			}
			else
			{
				availableDataNodesQ = new CircularQ<Data*>(maxDataObjectsRequired);
				availableSlotIdIndexObjsQ = new CircularQ<SlotIdSlotIndex*>(maxDataObjectsRequired);
				for(int i=0; i<maxDataObjectsRequired; ++i)
				{
					Data *node = new Data();
					availableDataNodesQ->addItemToPool(node);

					SlotIdSlotIndex *obj = new SlotIdSlotIndex();
					availableSlotIdIndexObjsQ->addItemToPool(obj);
				}
			}

			return initFlag;
		}

		DataId getDataCount()
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
			DataId begin = 0, end = 0;
			while(dataFileHandle.peek() != EOF)
			{
				startPos = dataFileHandle.tellg();
				begin = 0, end =0;
				readBlockForDataIndexing(begin, end);
				if(begin > end) // file is written in decreasing order of IDs
				{
					swap(begin, end);
				}
				DataIdRange range;
				range.setRange(begin,
								end,
								startPos);
				dataIdBlockRangeList.push_back(range);
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
			void reset()
			{
				slotId = -1, slotIndex = -1;
			}	

			void setValues(unsigned int &s, unsigned int &ind)
			{
				slotId = s;
				slotIndex = ind;
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
				// TODO : a check to see if the read node is valid
				curSize += node->getSize(BLOCK_SIZE);//sizeof(*node);
				if((curSize < BLOCK_SIZE || inMemoryCache) && dataFileHandle.tellg() != -1)
				{
					slots[slotId].push_back(node);
					//SlotIdSlotIndex dataInfo(slotId, slots[slotId].size()-1);
					SlotIdSlotIndex *dataInfo = getAvailableSlotIdSlotIndexObj();
					unsigned int ind = (slots[slotId].size()-1);
					dataInfo->setValues(slotId, ind);
					dataIdToSlotMap[node->getId()] = dataInfo;
					if(id == node->getId())
						slotIndex = slots[slotId].size()-1;
				}
				else
				{
					//availableDataNodesQ.push_back(node);
					availableDataNodesQ->addItemToPool(node);
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

		void readBlockForDataIndexing(DataId &begin, DataId &end)
		{
			DEBUG("\n readBlockForDataIndexing ");

			// Start reading from the file
			// Implementation Note :-
			// From the API perspective its good idea to stop reading 
			// when you reach the Block size and not when you reached 
			// the next block offset as specified by the data index 
			// data structure
			size_t curSize = 0;
			bool firstNode = true;
			Data* node = new Data();
			while(true)
			{
				node->reset();
				streampos beforeReadOff = dataFileHandle.tellg();
				//node->readNodeFromASCIIFile(dataFileHandle);
				node->readNodeFromBinaryFile(dataFileHandle);
				// TODO : a check to see if the read node is valid
				curSize += node->getSize(BLOCK_SIZE); //sizeof(*node);
				if((curSize < BLOCK_SIZE || inMemoryCache) && dataFileHandle.tellg() != -1)
				{
					if(firstNode)
					{
						firstNode = false;
						begin = node->getId();
					}
					end = node->getId();
				}
				else
				{
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
			delete node;
		}

		void dumpSlot(int slotId)
		{
			if(slots[slotId].size() < 1)
			{
				cout << "Empty Slot";
				return;
			}
			cout << "SLOT ID : " << slotId << "\n";
			cout << "\n SLOT DATA (SIZE : " << slots[slotId].size() << ") : \n";
			SLOT_ITERATOR it = slots[slotId].begin();
			for(; it != slots[slotId].end(); ++it)
			{
				cout << (*it)->getId() <<" ";
			}

			DATA_TO_SLOT_MAP_ITERATOR it2 = dataIdToSlotMap.begin();
			cout << "\n ID TO SLOT INDEX MAP (SIZE : " << dataIdToSlotMap.size() <<") : \n";
			for (;it2 != dataIdToSlotMap.end(); ++it2)
			{
				cout << it2->first << ":" ;
				cout << "(" << it2->second->slotId << "," << it2->second->slotIndex;
				cout << ")";
				cout << "\n";
			}
			cout << flush;
		}

		void invalidateSlot(const unsigned int slotId)
		{
			assert(slotId < NUM_SLOTS);

			//cout << "\n In invalidateSlot for slotId : " << slotId;
			//dumpSlot(slotId);
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
				if(dataIdToSlotMap.find((*it)->getId()) != dataIdToSlotMap.end() )
				{
					availableSlotIdIndexObjsQ->addItemToPool(dataIdToSlotMap[(*it)->getId()]);
					dataIdToSlotMap.erase((*it)->getId());
				}
				else
				{
					cout << "\n Error : Id " << (*it)->getId() << " exists in SLOTS but ";
					cout << " not found in dataIdToSlotMap";
					exit(-1);
				}
				availableDataNodesQ->addItemToPool(*it);
			}

			slots[slotId].clear();
		}

		Data* getAvailableDataNode()
		{
			Data *retVal = 0;
			if(!availableDataNodesQ->empty())
			{
				retVal = availableDataNodesQ->getItemFromPool();
				retVal->reset();
			}
			else
			{
				if(preAllocateFlag)
				{
					cout << "\n Warning : Allocating a new Data node!";
					cout << " Shouldn't happen if the nodes were pre-allocated";
				}
				retVal = new Data();
				availableDataNodesQ->addNewItemToPool(retVal);
			}
			return retVal;
		}
		SlotIdSlotIndex* getAvailableSlotIdSlotIndexObj()
		{
			SlotIdSlotIndex *retVal = 0;
			if(!availableSlotIdIndexObjsQ->empty())
			{
				retVal = availableSlotIdIndexObjsQ->getItemFromPool();
				// availableSlotIdIndexObjsQ.pop_front();
				retVal->reset();
			}
			else
			{
				if(preAllocateFlag)
				{
					cout << "\n Warning : Allocating a new SlotIdSlotIndex object!";
					cout << " Shouldn't happen if the nodes were pre-allocated";
				}
				retVal = new SlotIdSlotIndex();
				availableSlotIdIndexObjsQ->addNewItemToPool(retVal);
			}
			return retVal;
		}

		SlotIdSlotIndex* isInCache(const DataId id)
		{
			SlotIdSlotIndex *dataInfo = 0;
			DATA_TO_SLOT_MAP_ITERATOR it = dataIdToSlotMap.find(id);
			if(it != dataIdToSlotMap.end())
			{
				dataInfo = (it->second);
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

		// Use Vectors for slots because we want to exploit spatial 
		// locality and its optimized because indexing a file tells
		// us the number of items we are expecting in cache.
		typedef typename std::vector<Data*> SLOT;
		typedef typename std::vector<Data*>::iterator SLOT_ITERATOR;
		typedef typename boost::unordered_map<DataId, SlotIdSlotIndex*> DATA_TO_SLOT_MAP;
		typedef typename boost::unordered_map<DataId, SlotIdSlotIndex*>::iterator DATA_TO_SLOT_MAP_ITERATOR;

		const size_t BLOCK_SIZE;
		int NUM_SLOTS;
		string dataFileName;
		bool initFlag;
		unsigned int policy;
		bool writeBackFlag;
		unsigned int MAX_ITEMS_PER_SLOT;
		unsigned int MAX_ITEMS_IN_CACHE;
		DataId dataCount;
		bool inMemoryCache;
		bool preAllocateFlag;

		SLOT *slots;
		DATA_TO_SLOT_MAP dataIdToSlotMap;	

		ListNode *useListHead; // represents the LRU slot
		ListNode *useListTail; // represents the MRU slot
		
		vector<ListNode*> slotIdToListNodeMap;

		fstream dataFileHandle;
		fstream dataIndexFileHandle;

		vector<DataIdRange> dataIdBlockRangeList;

		// Object pool for BIG performance improvements
		// especially when we are dealing with millions of
		// data items.
		// Using circular queue to check performance improvement.
		// Custom circular queue is specially good for sparse graphs
		// which is the case with CDAGs because we can reserve a 
		// contiguous memory space which gives better spatial locality
		// than deques below.
		// deque<Data*> availableDataNodesQ;
		// deque<SlotIdSlotIndex*> availableSlotIdIndexObjsQ;
		


	public:
		// Implementing a fixed sized queue for pool handler
		// to verify if it gives some performance improvements
		template <typename T>
		class CircularQ
		{
		public:
			CircularQ():popPos(-1), 
					pushPos(-1),
					poolCapacity(0),
					curSize(0)
			{
				
			}

			CircularQ(unsigned int capacity):popPos(-1), 
												pushPos(-1),
												poolCapacity(0),
												curSize(0)
			{
				poolCapacity = capacity;
				poolVec.reserve(poolCapacity);
				T tempVal = 0;
				for(int i=0; i<poolCapacity; ++i)
				{
					poolVec.push_back(tempVal);
				}
			}

			~CircularQ()
			{
				for(int i=0; i < curSize; ++i)
				{
					//(poolVec[i])~T();
				}
			}

			void addNewItemToPool(T &item)
			{
				//cout << "\n called addNewItemToPool";
				if(!empty())
				{
					cout << "\n Error : Trying to add a new item to a non empty pool!";
					return;
				}
				//poolVec.push_back(item);
				
				if(popPos == -1)
				{
					popPos = 0;
				}
				++poolCapacity;
				poolVec.insert(poolVec.begin()+popPos, item);
				
				// ++curSize; // would be decremented in next call
				// getItemFromPool();
			}

			void addItemToPool(T item)
			{
				//cout << "\n called addItemToPool";
				
				if(curSize >= poolCapacity)
				{
					// TODO : maintain name of the pool allocator 
					// for better error messages
					cout << "\n Error : Tried to insert an item when the";
					cout << " pool is full.";
					return;
				}
				pushPos = (++pushPos) % poolCapacity;
				if(item == 0)
				{
					cout << "\n Null object being added back to item pool at "<<pushPos;
				}
				poolVec[pushPos] = item;
				++curSize;
				// cout << "\n push pos : " << pushPos;
				// cout << " popPos : " << popPos << " poolCapacity : " << poolCapacity << flush;
				// cout << " pool size : " << poolVec.size() << "cursize  : " << curSize << flush;
				if(popPos == -1)
				{
					popPos = 0;
				}
			}

			T getItemFromPool()
			{
				// cout << "\n called getItemFromPool";
				T retVal = 0;
				if(curSize <= 0)
				{
					cout << "\n Error : Tried getting an item from empty pool allocator";
					return retVal;
				}

				retVal = poolVec[popPos];
				if(!retVal)
				{
					cout << "\n Null object in pool";
				}
				// cout << "\n push pos : " << pushPos;
				// cout << " popPos : " << popPos << " poolCapacity : " << poolCapacity << flush;
				// cout << " pool size : " << poolVec.size() << "cursize  : " << curSize << flush;
				popPos = (++popPos) % poolCapacity;
				--curSize;
				return retVal;
			}

			bool empty()
			{
				// if(poolCapacity && curSize <=0)
				// {
				// 	cout << "\n pop pos = " << popPos << " pushPos : " << pushPos;
				// 	assert(popPos%poolCapacity == pushPos%poolCapacity);
				// }
				return curSize <= 0;
			}

			void printPoolHandlerQ()
			{
				cout << "\n Printing Pool Q :";
				for(int i=0, j=popPos; i < curSize; ++i, j=(++j % poolCapacity))
				{
					cout << poolVec[j] << " ";
				}
				cout << "\n";
			}
		private:
			unsigned int poolCapacity;
			std::vector<T> poolVec;
			int pushPos; // points to the last item inserted
			int popPos; // points to the element at the head of the queue
			unsigned int curSize;
		};
	
	private:
		CircularQ<Data*> *availableDataNodesQ;
		CircularQ<SlotIdSlotIndex*> *availableSlotIdIndexObjsQ;
	public:
		// test methods
		void testCircularQ()
		{
			cout << "\n Testing pool circular Q";

			{
				cout << "\n\nTest1";
				CircularQ<int> test(1);
				test.addItemToPool(1);
				cout << "\nAdd item to pool "; 
				cout << "\nFailed! Add item to pool ";
				test.addItemToPool(2);
				cout << "\nGet item from poool " << test.getItemFromPool();
				cout << "\nFailed! Get item from poool " << test.getItemFromPool();
			}

			{
				cout << "\n\n Test2";
				CircularQ<int> test(5);
				test.addItemToPool(1);
				test.addItemToPool(2);
				test.addItemToPool(3);
				test.addItemToPool(4);
				test.addItemToPool(5);
				test.printPoolHandlerQ();
				test.getItemFromPool();
				test.printPoolHandlerQ();
				test.addItemToPool(6);
				test.printPoolHandlerQ();
				test.getItemFromPool();
				test.printPoolHandlerQ();
				test.addItemToPool(7);
				test.printPoolHandlerQ();
				test.getItemFromPool();
				test.printPoolHandlerQ();
				test.addItemToPool(8);
				test.printPoolHandlerQ();

				while(!test.empty())
				{
					cout << test.getItemFromPool() << " ";
				}
			}
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
		
	};
}
