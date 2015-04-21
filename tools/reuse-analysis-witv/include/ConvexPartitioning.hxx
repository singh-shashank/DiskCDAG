#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>


#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <ctime>

#include <boost/unordered_set.hpp>
#include <boost/array.hpp>

#include "ddg/analysis/DiskCDAG.hxx"

using namespace std;

using namespace ddg;
using namespace llvm;
namespace ddg
{

#define PRED_INDEX 0
#define SUCC_INDEX 1

class ConvexPartitioning{


public:
    ConvexPartitioning()
    {
        cacheSize = 256/8;
        readyNodeCount = 0;
        bitSetIndex = 0;
        maxStaticId = 0;
        macroNodeCache = 0;
    }

    ~ConvexPartitioning()
    {
        if(readyNodesBitSet)
        {
            delete []readyNodesBitSet;
        }
        if(cdag)
        {
            delete cdag;
            cdag = 0;
        }
        if(macroNodeCache)
        {
            delete macroNodeCache;
            macroNodeCache = 0;
        }
        for(int i=0; i<ivMap.size(); ++i)
        {
            delete ivMap[i];
        }
    }

    struct ConvexComponent
    {
        static unsigned int tileId;
        deque<Id> nodesList;

        // IMPLEMENTATION NOTE :
        // ConvexComponent instances are expected not to have a huge memory
        // foot print because it is expected to contain just enough nodes
        // to fit a passed in cache size.
        // Now with neighbors and successor list we needed the following
        // operations :
        // 1) Insertion/Enqueue 2) Dequeue 3) Lookup 4) Erase
        // Everything except erase is achieved in constant time by
        // using a unordered map and deque.
        deque<Id> readyNeighborsList;
        //boost::unordered_set<Id> idToNeighborsListMap;

        deque<Id> readySuccsList;
        //boost::unordered_set<Id> idToSuccsListMap;

        unsigned long takenNeighbors;
        unsigned long takenSuccs;

        ConvexComponent()
        {
            takenNeighbors = 0;
            takenSuccs = 0;
        }

        void reset()
        {
            readyNeighborsList.clear();
            readySuccsList.clear();
            nodesList.clear();
            //idToNeighborsListMap.clear();
            //idToSuccsListMap.clear();
            ++ConvexComponent::tileId;
        }

        void writeNodesOfComponent(ostream &out, DiskCDAG<GraphNodeWithIV> *dag)
        {
            // sort nodes based on their dynamic ids
            sort(nodesList.begin(), nodesList.end());
            out << "Tile " << ConvexComponent::tileId;
            for(deque<Id>::iterator it = nodesList.begin();
                it != nodesList.end(); ++it)
            {
                GraphNodeWithIV *temp = dag->getNode(*it);
                out << "\nStatic ID: " << temp->staticId << ";";
                out << "Dyn ID: " << temp->dynId << ";";
                out << " " << llvm::Instruction::getOpcodeName(temp->type) << ";";
            }
            out << "\n";
            out << flush;
        }
    };

    void init(const std::string& llvmBCFilename)
    {
        llvm_shutdown_obj shutdownObj;  // Call llvm_shutdown() on exit.
        LLVMContext &context = getGlobalContext();

        SMDiagnostic err;
        auto_ptr<Module> module; 
        module.reset(ParseIRFile(llvmBCFilename, err, context));

        if (module.get() == 0) {
        //err.print(argv[0], errs());
           return;
        }

        Ids ids;
        ids.runOnModule(*module.get());
        maxStaticId = ids.getNumInsts();
        cout << "\n Max Static Id = " << maxStaticId;

        const string programName = llvmBCFilename.substr(0, llvmBCFilename.find("."));

        clock_t begin = clock();
        cdag = DiskCDAG<GraphNodeWithIV>::generateGraph<GraphNodeWithIV, DiskCDAGBuilderWithIV>(ids, programName); 
        clock_t end = clock();
        double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
        cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;
        readyNodesBitSet = 0;

        if(cdag)
        {
            cdag->printDOTGraph("diskgraph.dot");
            numNodes = cdag->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            readyNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            
            writeOriginalMemTrace();
            writeOriginalMemTraceWithPool();            
        }
        else
        {
          std::cerr << "Fatal : Failed to generate CDAG \n";
        }
    }

    bool isSpecialNode(Id &curNodeId)
    {
        return (cdag->getNode(curNodeId)->succsList.size() == 1 && 
           cdag->getNode(curNodeId)->type != Instruction::Load &&
           cdag->getNode(curNodeId)->addr < maxStaticId);
    }

    bool isSpecialChain(Id &curNodeId)
    {
        bool retVal = true;
        for(deque<Id>::iterator it = cdag->getNode(curNodeId)->predsList.begin(); it != cdag->getNode(curNodeId)->predsList.end(); ++it)
        {
            if((!isSpecialNode(*it) &&
             nodeIdToUnprocPredsSuccsCountMap[*it][PRED_INDEX] != -1) || (isSpecialNode(*it) && !isReady(*it)))
            {
                retVal = false;
                break;
            }
        }
        return retVal;
    }  

    void createMacroNode(Id &curNodeId, deque<Id> &microNodeList)
    {
        int predCount = cdag->getNode(curNodeId)->predsList.size();
        for(int i=0; i<predCount; ++i)
        {
            // If predecessor is special node then it can 
            // be added to the macro node
            Id predId = cdag->getNode(curNodeId)->predsList[i];
            if(isSpecialNode(predId))
            {
                microNodeList.push_back(predId);
                createMacroNode(predId, microNodeList);
            }
        }
    }

    void writeMacroNodeInformation()
    {
        BYTE *visitedNodeBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
        memset(visitedNodeBitSet, 0, numOfBytesForReadyNodeBitSet);
        string macroNodeListFileName = "diskgraphmacronodeinfo";

        ofstream macroNodeListFile(macroNodeListFileName.c_str(), fstream::binary);
        deque<Id> microNodeList;

        for(int i=numNodes-1; i >= 0; --i)
        {
            microNodeList.clear();
            if(!utils::isBitSet(visitedNodeBitSet, i, numOfBytesForReadyNodeBitSet))
            {
                createMacroNode(i, microNodeList);
            }

            // Write Macro node to a file
            macroNodeListFile.write((const char*)&i, sizeof(Id));
            Id microNodesCount = microNodeList.size();
            macroNodeListFile.write((const char*)&microNodesCount, sizeof(Id));
            for(int j=0; j<microNodesCount; ++j)
            {
                macroNodeListFile.write((const char*)&microNodeList[j], sizeof(Id));
                // Also mark these nodes as visited
                utils::setBitInBitset(visitedNodeBitSet, microNodeList[j], numOfBytesForReadyNodeBitSet);
            }
        }
        macroNodeListFile.close();

        // Initialize the macro node cache
        macroNodeCache = new DiskCache<DataList, Id>(1024, 4);
        if(!macroNodeCache->init(macroNodeListFileName))
        {
            cout << "\n Cache initialization failed for macro nodes list...";
            return;
        }

        // // Test Code
        // cout << "\n Printing macro node information...";
        // for(int i=0; i<numNodes; ++i)
        // {
        //     macroNodeCache->getData(i)->print(cout);
        // }
    }

    void generateConvexComponents(unsigned int cacheS,
                                    unsigned int nPC,
                                    unsigned int sPC)
    {
        ofstream cur_proc_node("cur_proc_node");

        idToNeighborsListMap = new bool[numNodes];
        memset(idToNeighborsListMap, 0, sizeof(bool)*numNodes);
        idToSuccsListMap = new bool[numNodes];
        memset(idToSuccsListMap, 0, sizeof(bool)*numNodes);


        prepareInitialReadyNodes();
        cout <<"\n Initial ready node count = " << readyNodeCount;

        return;

        outFile.open("convex_out_file");

        ofstream prog("progress");
        int processedNodeCount = 0;
        int prev = -1;
        unsigned int liveSetSize = 0;
        bool firstPass = true;
        cout << "\n";
        
        Id nodeId = 0;

        ConvexComponent curConvexComp;
        set<Id> liveSet;


        while(readyNodeCount > 0)
        {
            bool empQ = false;
            curConvexComp.reset();
            nodeId = selectReadyNode(empQ);
            if(empQ)
            {
                cout << "\n Ready Node Count : " << readyNodeCount;
                cout << "But ready node queue is empty!...exiting";
                cout << flush;
                break;
            }
            //cout << "\n\nSelected node (from selectReadyNode): " << nodeId;
            curConvexComp.nodesList.push_back(nodeId);
            // set the bit for the node marking it processed
            utils::setBitInBitset(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);
            --readyNodeCount;

            // Update the unprocessed predecessor count for current node's 
            // successor here. For optimization its being done in the call
            // to updateListOdReadyNodes() method before checking if succ
            // has no more unprocessed predecessors
            updateListOfReadyNodes(cdag->getNode(nodeId));

            //cout << "\nConvex component with id : " << ConvexComponent::tileId;

            clock_t beg = clock();

            cur_proc_node << nodeId << " (";
            cur_proc_node << double(clock() - beg) / (CLOCKS_PER_SEC * 60);
            cur_proc_node << ")";
            
            

            prog.seekp(0, ios::beg);
            prog << processedNodeCount;
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
        //assert(readyNodeQ.size() == 0);
        cout <<"\n Ready Node Count (in the end) : " << readyNodeCount;
        cur_proc_node.close();
        remove("cur_proc_node");

    }

    void printDeque(deque<Id> &mydeque, string qname)
    {
        cout <<"\n"<<qname << ":";
        for (std::deque<Id>::iterator it = mydeque.begin(); it!=mydeque.end(); ++it)
            std::cout << ' ' << *it;
    }

    void updateListOfReadyNodes(GraphNodeWithIV *node)
    {
        int succCount = node->succsList.size();
        for(int i=0; i<succCount; ++i)
        {
            // If this successor has no more unprocessed predecessor 
            // then its ready

            //GraphNodeWithIV *succNode = node->succsList[i];
            Id succId = node->succsList[i];
            
            // First decrease the count by 1
            if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] > 0)
            {
                --nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX];
                // Now check if there are any unprocessed nodes
                if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] == 0)
                {
                    // mark this successor as ready
                    utils::unsetBitInBitset(readyNodesBitSet, succId, numOfBytesForReadyNodeBitSet);
                    readyNodeQ.push_back(succId);
                    ++readyNodeCount;
                    //cout <<"\n In updateListOfReadyNodes, successor is now ready : " << succId;
                }
            }
        }
    }

    void postProcessIV(deque<Id> &iv, const unsigned int &fixedLen)
    {

    }

    void prepareInitialReadyNodes()
    {
        // First set all the bits i.e. mark all nodes as 'not' ready
        memset(readyNodesBitSet, ~0, numOfBytesForReadyNodeBitSet);

        // Traverse over the graph once to mark all the nodes as ready
        // that have 0 preds i.e. all input vertices
        nodeIdToUnprocPredsSuccsCountMap.reserve(numNodes);

        // Setup the IV map for fast lookup
        unsigned int fixedLen = 2*(cdag->maxDepth)+1;
        cout << "\n Max Depth Value : " << cdag->maxDepth;
        
        ivMap.reserve(numNodes);
        for(int i=0; i<numNodes; ++i)
        {
            NodeIVInfo *tempIVInfo = new NodeIVInfo(i, fixedLen);
            ivMap.push_back(tempIVInfo);
        }

        for(int i=0; i<numNodes; ++i)
        {            
            // Store IV for this node in the map
            assert(ivMap[i]->itVec.size() >= cdag->getNode(i)->loopItVec.size());
            for(int j=0; j<cdag->getNode(i)->loopItVec.size(); ++j)
            {
                ivMap[i]->itVec[j+1] = cdag->getNode(i)->loopItVec[j];
            }

            // Find all nodes ready for processing 
            if(cdag->getNode(i)->predsList.size() == 0)
            {
                // a node with zero predecessors
                // it should be the load node
                assert(cdag->getNode(i)->type == 27); 
                // mark this node as ready i.e. unset the corresponding bit
                utils::unsetBitInBitset(readyNodesBitSet, i, numOfBytesForReadyNodeBitSet);
                readyNodeQ.push_back(i);
                ++readyNodeCount;
            }
            boost::array<Id,1> temp = {{cdag->getNode(i)->predsList.size()}};
            nodeIdToUnprocPredsSuccsCountMap.push_back(temp);
        }

        // sort the IV map

        std::sort(begin(ivMap), end(ivMap), ConvexPartitioning::compareNodeIVInfo);

        // Iterate over the iv map and fill in the index map
        nodeIdToIvMapIndexMap.reserve(numNodes);
        for(int i=0; i < ivMap.size(); ++i)
        {
            nodeIdToIvMapIndexMap[ivMap[i]->nodeId] = i;
        }
    }

    Id selectReadyNode(bool &empQ)
    {
        Id retVal = 0;
        empQ = false;
        // readyNodeQ will have some processed nodes which
        // were picked up from the ready neighbor or ready
        // successor list. Don't return them here. they 
        // should be removed from the queue
        while(!readyNodeQ.empty() && !isReady(readyNodeQ.front()))
        {
            readyNodeQ.pop_front();
        }
        if(!readyNodeQ.empty())
        {
            retVal = readyNodeQ.front();
            readyNodeQ.pop_front();
        }
        else
        {
            empQ = true;
        }
        return retVal;
    }

    inline bool isReady(Id &nodeId)
    {
        // A node is ready if the corresponding bit is set to 0.
        // isSetBit checks if the bit is set to 1.
        return (!utils::isBitSet(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet));
    }

    DiskCDAG<GraphNodeWithIV>* getCdagPtr()
    {
        return cdag;
    }

    void writeOriginalMemTrace()
    {
        ofstream origMemTraceFile("original_memtrace.txt");
        for(int i=0; i<numNodes; ++i)
        {
            GraphNodeWithIV *curNode = cdag->getNode(i);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(int j=0; j < predCount; ++j)
                {
                    GraphNodeWithIV *predNode = cdag->getNode(curNode->predsList[j]);
                    origMemTraceFile << predNode->addr << "\n";
                }
                origMemTraceFile << curNode->addr << "\n";
            }
        }
    }

    void assignAddr(Address *addr, deque<Address> &freeAddr, Address &nextAddr)
    {
        *addr = nextAddr++;
        return;
        if(freeAddr.empty())
        {
            *addr = nextAddr++;
        }
        else
        {
            *addr = freeAddr.front();
            freeAddr.pop_front();
        }
    }

    void writeOriginalMemTraceWithPool()
    {
        Address *addr = new Address[numNodes]();
        int *remUses = new int[numNodes];
        for(Id i=0; i<numNodes; ++i)
        {
            remUses[i] = cdag->getNode(i)->succsList.size();
        }
        deque<Address> freeAddr;
        Address nextAddr = 1;
        ofstream memTraceFile("orig_memtrace_withpool.txt");
        int numOfCC = convexComponents.size();
        for(int i=0; i<numNodes; ++i)
        {
            GraphNodeWithIV *curNode = cdag->getNode(i);
            size_t currId = curNode->dynId;
            assert(currId < numNodes);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(int k=0; k < predCount; ++k)
                {
                    GraphNodeWithIV *predNode = cdag->getNode(curNode->predsList[k]);
                    int pred = predNode->dynId;
                    assert(pred < numNodes);
                    if(addr[pred] == 0)
                    {
                        assignAddr(&(addr[pred]), freeAddr, nextAddr);
                    }
                    memTraceFile << addr[pred] << "\n";
                    assert(remUses[pred] > 0);
                    if(--remUses[pred] == 0)
                    {
                      freeAddr.push_back(addr[pred]);
                    }
                }
                assignAddr(&(addr[currId]), freeAddr, nextAddr);
                memTraceFile << addr[currId] << "\n";
            }
        }

        delete []addr;
        delete []remUses;
    }

    void writeMemTraceForScheduleWithPool()
    {
        Address *addr = new Address[numNodes]();
        int *remUses = new int[numNodes];
        for(Id i=0; i<numNodes; ++i)
        {
            remUses[i] = cdag->getNode(i)->succsList.size();
        }
        deque<Address> freeAddr;
        Address nextAddr = 1;
        ofstream memTraceFile("memtrace_withpool.txt");
        int numOfCC = convexComponents.size();
        for(int i=0; i<numOfCC; ++i)
        {
            int numNodesInCC = convexComponents[i].size();
            for(int j=0; j<numNodesInCC; ++j)
            {
                GraphNodeWithIV *curNode = cdag->getNode(convexComponents[i][j]);
                size_t currId = curNode->dynId;
                assert(currId < numNodes);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(int k=0; k < predCount; ++k)
                    {
                        GraphNodeWithIV *predNode = cdag->getNode(curNode->predsList[k]);
                        int pred = predNode->dynId;
                        assert(pred < numNodes);
                        if(addr[pred] == 0)
                        {
                            assignAddr(&(addr[pred]), freeAddr, nextAddr);
                        }
                        memTraceFile << addr[pred] << "\n";
                        assert(remUses[pred] > 0);
                        if(--remUses[pred] == 0)
                        {
                          freeAddr.push_back(addr[pred]);
                        }
                    }
                    assignAddr(&(addr[currId]), freeAddr, nextAddr);
                    memTraceFile << addr[currId] << "\n";
                }
            }
        }

        delete []addr;
        delete []remUses;
    }

    void writeMemTraceForSchedule()
    {
        ofstream memTraceFile("memtrace.txt");
        int numOfCC = convexComponents.size();
        for(int i=0; i<numOfCC; ++i)
        {
            int numNodesInCC = convexComponents[i].size();
            for(int j=0; j<numNodesInCC; ++j)
            {
                GraphNodeWithIV *curNode = cdag->getNode(convexComponents[i][j]);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(int k=0; k < predCount; ++k)
                    {
                        GraphNodeWithIV *predNode = cdag->getNode(curNode->predsList[k]);
                        memTraceFile <<predNode->addr << "\n";
                    }
                    memTraceFile <<curNode->addr << "\n";
                }
            }
        }
    }

    

private:
    DiskCDAG<GraphNodeWithIV> *cdag;
    unsigned int cacheSize;
    ofstream outFile;

    struct NodeIVInfo
    {
        Id nodeId;
        vector<unsigned int> itVec;
        NodeIVInfo(Id id, unsigned int fixedLen) :itVec()
        {
            nodeId = id;
            itVec.reserve(fixedLen);
            for(int i=0; i<fixedLen; ++i)
            {
                itVec.push_back(0);
            }
        }

        bool operator<(const NodeIVInfo& node) const
        {
            bool isEqual = true;
            // Ignore the first index for comparison
            // Compare the i,j,k values i.e. odd indices in vector
            for(int i=0; i<node.itVec.size(); ++i)
            {
                if (this->itVec[i] == node.itVec[i])
                {
                    continue;
                }
                else if (this->itVec[i] > node.itVec[i])
                {
                    return false;
                }
                else 
                {
                    return true;
                }
            }
            return false;
        }

        void print(ostream &out)
        {
            out << "\n Id : " <<nodeId << " < ";
            for(int i=0; i<itVec.size(); ++i)
            {
                out << itVec[i] << " ";
            }
            out << " > ";
        }
    };

    static bool compareNodeIVInfo(const NodeIVInfo *lhs,
        const NodeIVInfo* rhs) 
    {
        
        if(lhs && rhs && lhs != rhs && lhs->nodeId != rhs->nodeId)
        {
            return (*lhs < *rhs);
        }
        return false;
    } 
    
    vector<NodeIVInfo*> ivMap;
    vector<Id> nodeIdToIvMapIndexMap;

    deque< deque<Id> > convexComponents;

    size_t numOfBytesForReadyNodeBitSet;
    BYTE *readyNodesBitSet; // a bit set for quick lookup
    Id readyNodeCount;
    unsigned int bitSetIndex;
    //boost::unordered_map<Id, bool>; // a map for quick lookup

    // Implementation Note :
    // Assuming 8 byte for Id, this map for about 20 million nodes
    // will occupy roughly 457 MB of memory. If its expected to 
    // process more nodes then we should be writing and reading this
    // map from the file by making use of the DiskCache.
    //boost::unordered_map<Id, boost::array<Id, 2> > nodeIdToUnprocPredsSuccsCountMap;
    vector<boost::array<int, 1> > nodeIdToUnprocPredsSuccsCountMap;

    deque<Id> readyNodeQ;

    bool *idToNeighborsListMap;
    bool *idToSuccsListMap;
    Id maxStaticId;

    size_t numNodes;
    DiskCache<DataList, Id> *macroNodeCache;

private:

    int binarySearchForIV(Id nodeId, NodeIVInfo &ptr)
    {
        int beg = nodeIdToIvMapIndexMap[nodeId]; 
        int retInd = beg;
        int end = ivMap.size()-1;
        while(beg < end)
        {
            int mid = (beg+end)/2;
            //if(ivMap[mid])
        }
        return retInd;
    }
};

unsigned int ConvexPartitioning::ConvexComponent::tileId = 0;
}