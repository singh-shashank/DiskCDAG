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
        boost::unordered_set<Id> idToNeighborsListMap;

        deque<Id> readySuccsList;
        boost::unordered_set<Id> idToSuccsListMap;

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
            idToNeighborsListMap.clear();
            idToSuccsListMap.clear();
            ++ConvexComponent::tileId;
        }

        void writeNodesOfComponent(ostream &out, DiskCDAG *dag)
        {
            // sort nodes based on their dynamic ids
            sort(nodesList.begin(), nodesList.end());
            out << "Tile " << ConvexComponent::tileId;
            for(deque<Id>::iterator it = nodesList.begin();
                it != nodesList.end(); ++it)
            {
                DiskCDAG::CDAGNode *temp = dag->getNode(*it);
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

        const string programName = llvmBCFilename.substr(0, llvmBCFilename.find("."));

        clock_t begin = clock();
        cdag = DiskCDAG::generateGraph(ids, programName); 
        clock_t end = clock();
        double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
        cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;

        if(cdag)
        {
            cdag->printDOTGraph("diskgraph.dot");
            numOfBytesForReadyNodeBitSet = cdag->getNumOfBytesForNodeMarkerBS();
            readyNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            numNodes = cdag->getNumNodes();
            writeOriginalMemTrace();

            // begin = clock();
            // // cdag->performBFSWithoutQ("bfsOut");
            // //cdag->performBFS("bfsOut");
            // end = clock();
            // elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
            // cout << "\n Time taken for BFS traversal (in mins) : " << elapsed_time / 60;


            
        }
        else
        {
          std::cerr << "Fatal : Failed to generate CDAG \n";
        }
    }

    void generateConvexComponents(unsigned int cacheS,
                                    unsigned int nPC,
                                    unsigned int sPC)
    {
        cacheSize = cacheS;
        neighborPriorityCount = nPC;
        successorPriorityCount = sPC;
        selectNeighborFlag = neighborPriorityCount >= successorPriorityCount ? true : false;

        prepareInitialReadyNodes();
        cout <<"\n Initial ready node count = " << readyNodeCount;

        outFile.open("convex_out_file");

        ofstream prog("progress");
        int processedNodeCount = 0;
        int prev = -1;
        cout << "\n";
        
        Id nodeId = 0;

        ConvexComponent curConvexComp;
        set<Id> liveSet;

        while(readyNodeCount > 0)
        {
            nodeId = selectReadyNode();
            DiskCDAG::CDAGNode *curNode = cdag->getNode(nodeId);
            //cout << "\n\nSelected node (from selectReadyNode): " << nodeId;

            // we have valid nodeId for a ready node
            curConvexComp.reset();
            liveSet.clear();
            //cout << "\nConvex component with id : " << ConvexComponent::tileId;

            // The ready node is already selected in the variable 'node'
            while(readyNodeCount > 0 && updateLiveSet(liveSet, curNode))
            {
                // add it current convex component
                curConvexComp.nodesList.push_back(nodeId);

                // set the bit for the node marking it processed
                utils::setBitInBitset(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);
                --readyNodeCount;


                // Update the unprocessed predecessor count for current node's 
                // successor here. For optimization its being done in the call
                // to updateListOdReadyNodes() method before checking if succ
                // has no more unprocessed predecessors
                updateListOfReadyNodes(curNode);

                // And we also need to decrease the unprocessed successor count 
                // for its predecessor here
                for(int i=0; i<curNode->predsList.size(); ++i)
                {
                    Id predId = curNode->predsList[i];
                    if(nodeIdToUnprocPredsSuccsCountMap[predId][SUCC_INDEX] > 0)
                    {
                        --nodeIdToUnprocPredsSuccsCountMap[predId][SUCC_INDEX];
                    }
                }

                // Get the next ready node to add to the convex component
                nodeId = selectBestNode(curConvexComp, curNode);
                //cout << "\n\nSelected node (from selectBestNode): " << nodeId;
                if(nodeId == 0)
                {
                    break;
                }
                else
                {
                    if(curNode->getId() == nodeId)
                    {
                        //cout << "\n Error selected the same node...";
                        return;
                    }
                    curNode = cdag->getNode(nodeId);
                }
            }
            convexComponents.push_back(curConvexComp.nodesList);
            //curConvexComp.writeNodesOfComponent(outFile, cdag);
            //cout << "\n end of one convex component. live set size = " <<liveSet.size();
            processedNodeCount += curConvexComp.nodesList.size();
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

    }

    void printDeque(deque<Id> &mydeque, string qname)
    {
        cout <<"\n"<<qname << ":";
        for (std::deque<Id>::iterator it = mydeque.begin(); it!=mydeque.end(); ++it)
            std::cout << ' ' << *it;
    }

    Id selectBestNode(ConvexComponent &cc,
        DiskCDAG::CDAGNode *curNode)
    {
        //cout <<"\nIn selectBestNode...";
        Id nextNodeId = 0;
        // Update readyNeighborsList and readySuccsList
        // i.e. neighbors(curNode) INTERSECTION ReadyNodeSet
        // neighbors(curNode) is union of all the predecessors of curNode successors
        Id succCount = curNode->succsList.size();
        for(int i=0; i<succCount; ++i)
        {
            DiskCDAG::CDAGNode *succNode = cdag->getNode(curNode->succsList[i]);

            // Neigbhor list update
            Id succPredCount = succNode->predsList.size();
            for(int j=0; j<succPredCount; ++j)
            {
                // we have Id of a neighbor of curNode
                // check that this neighbor satisfies two condtions:
                // - its NOT in neigbors list of this component
                // - and its ready
                if(cc.idToNeighborsListMap.find(succNode->predsList[j]) == cc.idToNeighborsListMap.end()
                    && isReady(succNode->predsList[j])
                    )
                {
                    // this neighbor can now be added to the ready neighbor list
                    cc.readyNeighborsList.push_back(succNode->predsList[j]);
                    cc.idToNeighborsListMap.insert(succNode->predsList[j]);
                    //cout << "\n Adding neighbor to ready list with id : " << succNode->predsList[j];
                    //printDeque(cc.readyNeighborsList, "n-list");
                }

            }

            // Successor List update
            if(cc.idToSuccsListMap.find(curNode->succsList[i]) == cc.idToSuccsListMap.end() 
                && isReady(curNode->succsList[i]))
            {
                // this successor can now be added to ready successor list
                cc.readySuccsList.push_back(curNode->succsList[i]);
                cc.idToSuccsListMap.insert(curNode->succsList[i]);
                //cout << "\n Adding successor to ready list with id : " << curNode->succsList[i];
                //printDeque(cc.readySuccsList, "s-list");
            }
        }

        // Remove curNode from ready lists
        //cout << "\nRemove node from ready lists : " << curNode->getId();
        if(cc.idToNeighborsListMap.find(curNode->getId()) != cc.idToNeighborsListMap.end())
        {
            //cout <<"\n Removing from neighbors list...";
            cc.readyNeighborsList.erase(find(cc.readyNeighborsList.begin(), cc.readyNeighborsList.end(), curNode->getId()));
            cc.idToNeighborsListMap.erase(curNode->getId());
            //printDeque(cc.readyNeighborsList, "n-list");
        }
        if(cc.idToSuccsListMap.find(curNode->getId()) != cc.idToSuccsListMap.end())
        {
            //cout <<"\n Removing from successors list...";
            cc.readySuccsList.erase(find(cc.readySuccsList.begin(), cc.readySuccsList.end(), curNode->getId()));
            cc.idToSuccsListMap.erase(curNode->getId());
            //printDeque(cc.readySuccsList, "s-list");
        }

        // Determine the next ready node to be returned
        //cout << "\n";
        //cout << "Neighbors turn ? " << selectNeighborFlag;
        //cout << " readyNeighborsList size : " <<  cc.readyNeighborsList.size();
        //cout << " readySuccsList size : " << cc.readySuccsList.size();
        //cout << " takenNeighbors : " << cc.takenNeighbors;
        //cout << " takenSuccs : " << cc.takenSuccs;
        if(cc.readyNeighborsList.size() > 0 || cc.readySuccsList.size() > 0)
        {
            // Choose between a neighbor or a successor depending on the
            // priority
            if(selectNeighborFlag) 
            {
                //cout << "\nNeighbor turn...";
                // It's ready neighbors turn...
                if(cc.readyNeighborsList.size() > 0)
                {
                    // ... and there are neighbors.

                    // no need to pop and erase the next node here
                    // because in next call this will be erased
                    // or if there is no next call then this data structure
                    // will be reset before use.
                    nextNodeId = cc.readyNeighborsList.front();
                    ++cc.takenNeighbors;
                    if(cc.takenNeighbors >= neighborPriorityCount)
                    {
                        cc.takenNeighbors = 0;
                        selectNeighborFlag = false;
                    }
                    //cout << "selected neighbor with id : " <<nextNodeId;
                }
                else
                {
                    // but we ran out of neighbors..return ready successors
                    // till we get a neighbor.
                    // Do not increment takenSuccs count.
                    nextNodeId = cc.readySuccsList.front();
                    //cout << "selected successor with id : " <<nextNodeId;
                }
            }
            else 
            {
                //cout <<"\nSuccessor turn...";
                // It's ready successors turn...
                if(cc.readySuccsList.size() > 0)
                {
                    // ... and there are successors
                    nextNodeId = cc.readySuccsList.front();
                    ++cc.takenSuccs;
                    if(cc.takenSuccs >= successorPriorityCount)
                    {
                        cc.takenSuccs = 0;
                        selectNeighborFlag = true;
                    }
                    //cout << "selected successor with id : "<<nextNodeId;
                }
                else
                {
                    // but we ran out of successors..return ready neighbors
                    // till we get a successor.
                    // Do not increment takenNeighbors count.
                    nextNodeId = cc.readyNeighborsList.front();
                    //cout << "selected neighbor with id : " <<nextNodeId;
                }
            }
            // cout << "\n";
            // if(cc.takenNeighbors == neighborPriorityCount && 
            //     cc.takenSuccs == successorPriorityCount)
            // {
            //     cc.takenNeighbors = 0;
            //     cc.takenSuccs = 0;
            // }

            // if(cc.takenSuccs < successorPriorityCount)
            // {
            //     if(cc.readySuccsList.size() > 0)
            //     {
            //         nextNodeId = cc.readySuccsList.front();
            //         ++cc.takenSuccs;
            //         cout << "selected successor with id : "<<nextNodeId;
            //     }
            //     else
            //     {
            //         nextNodeId = cc.readyNeighborsList.front();
            //         ++cc.takenNeighbors;
            //         cout << "selected neighbor with id : " <<nextNodeId;
            //     }
            // }
            // else if(cc.takenNeighbors < neighborPriorityCount)
            // {
            //     if(cc.readyNeighborsList.size() > 0)
            //     {
            //         nextNodeId = cc.readyNeighborsList.front();
            //         ++cc.takenNeighbors;
            //         cout << "selected neighbor with id : "<<nextNodeId;
            //     }
            //     else
            //     {
            //         nextNodeId = cc.readySuccsList.front();
            //         ++cc.takenSuccs;
            //         cout << "selected successor with id : " <<nextNodeId;
            //     }
            // }
        }
        else
        {
            // There are no ready neighbors or successors just yet
            // get the next ready node
            // nextNodeId = selectReadyNode();
        }
        return nextNodeId;
    }

    bool updateLiveSet(set<Id> &liveSet, DiskCDAG::CDAGNode *curNode)
    {
        //cout << "\nIn updateLiveSet...";
        bool retVal = true;
        set<Id> oldLiveSet = liveSet;

        // Birth of a successor node
        Id succCount = curNode->succsList.size();
        for(int i=0; i<succCount; ++i)
        {
            // If this successor is still unprocessed add it to the live set
            if(nodeIdToUnprocPredsSuccsCountMap[curNode->succsList[i]][PRED_INDEX] > 0)
            {
                liveSet.insert(curNode->succsList[i]);
                //cout << "\nBirth of a successor node : " << curNode->succsList[i];
            }
        }

        // Resurrecting a predecessor node or killing it if curNode was 
        // the last unprocessed successor for this predecessor
        Id predCount = curNode->predsList.size();
        for(int i=0; i<predCount; ++i)
        {
            bool hasAnyUnprocSuccs = false;
            DiskCDAG::CDAGNode *predNode = cdag->getNode(curNode->predsList[i]);
            Id succCountForPredNode = predNode->succsList.size();
            for(int j=0; j<succCountForPredNode; ++j)
            {
                if(nodeIdToUnprocPredsSuccsCountMap[predNode->succsList[j]][PRED_INDEX] > 0)
                {
                    // Yes this successor of the current predecessor is still unprocessed
                    liveSet.insert(predNode->getId());
                    hasAnyUnprocSuccs = true;
                    //cout << "\nResurrecting node : " << predNode->getId();
                    break;
                }
            }
            if(!hasAnyUnprocSuccs)
            {
                // If all the successors of this predecessors are processed then we 
                // can remove this predecessor from the liveset
                liveSet.erase(predNode->getId());
                //cout << "\nRemoving died node : " << predNode->getId();
            }
        }

        // Compare if we haven't exceeded the tile size
        if(liveSet.size() > cacheSize)
        {
            liveSet = oldLiveSet;
            retVal = false;
        }
        //cout << "\nExiting updateLiveSet with retVal = " << retVal;
        return retVal;
    }

    void updateListOfReadyNodes(DiskCDAG::CDAGNode *node)
    {
        int succCount = node->succsList.size();
        for(int i=0; i<succCount; ++i)
        {
            // If this successor has no more unprocessed predecessor 
            // then its ready

            //DiskCDAG::CDAGNode *succNode = node->succsList[i];
            Id succId = node->succsList[i];
            
            // First decrease the count by 1
            if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] > 0)
            {
                --nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX];
            }

            // Now check if there are any unprocessed nodes
            if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] <= 0)
            {
                // mark this successor as ready
                utils::unsetBitInBitset(readyNodesBitSet, succId, numOfBytesForReadyNodeBitSet);
                ++readyNodeCount;
                //cout <<"\n In updateListOfReadyNodes, successor is now ready : " << succId;
            }
        }
    }

    void prepareInitialReadyNodes()
    {
        // First set all the bits i.e. mark all nodes as 'not' ready
        memset(readyNodesBitSet, ~0, numOfBytesForReadyNodeBitSet);

        // Traverse over the graph once to mark all the nodes as ready
        // that have 0 preds i.e. all input vertices
        for(int i=0; i<numNodes; ++i)
        {
            DiskCDAG::CDAGNode *node = cdag->getNode(i);
            if(node->predsList.size() == 0)
            {
                // a node with zero predecessors
                // it should be the load node
                assert(node->type == 27); 
                // mark this node as ready i.e. unset the corresponding bit
                utils::unsetBitInBitset(readyNodesBitSet, i, numOfBytesForReadyNodeBitSet);
                ++readyNodeCount;
            }
            boost::array<Id,2> temp = {{node->predsList.size(), node->succsList.size()}};
            nodeIdToUnprocPredsSuccsCountMap[i] = temp;
        }
    }

    // void writeConvexComponentsToFile(const char* filename)
    // {
    //     ofstream file(filename);
    //     unsigned int numOfComps = convexComponents.size();
    //     for(unsigned int i=0; i<numOfComps; ++i)
    //     {
    //         convexComponents[i].writeNodesOfComponent(file);
    //         cout << "\n";
    //     }
    // }

    Id selectReadyNode()
    {
        Id retVal = 0;
        cdag->getFirstReadyNode(readyNodesBitSet, retVal, bitSetIndex);
        return retVal;
    }

    inline bool isReady(Id &nodeId)
    {
        // A node is ready if the corresponding bit is set to 0.
        // isSetBit checks if the bit is set to 1.
        return (!utils::isBitSet(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet));
    }

    DiskCDAG* getCdagPtr()
    {
        return cdag;
    }

    void writeOriginalMemTrace()
    {
        ofstream origMemTraceFile("original_memtrace.txt");
        for(int i=0; i<numNodes; ++i)
        {
            DiskCDAG::CDAGNode *curNode = cdag->getNode(i);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(int j=0; j < predCount; ++j)
                {
                    DiskCDAG::CDAGNode *predNode = cdag->getNode(curNode->predsList[j]);
                    origMemTraceFile << predNode->addr << "\n";
                }
                origMemTraceFile << curNode->addr << "\n";
            }
        }
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
                DiskCDAG::CDAGNode *curNode = cdag->getNode(convexComponents[i][j]);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(int k=0; k < predCount; ++k)
                    {
                        DiskCDAG::CDAGNode *predNode = cdag->getNode(curNode->predsList[k]);
                        memTraceFile << predNode->addr << "\n";
                    }
                    memTraceFile << curNode->addr << "\n";
                }
            }
        }
    }

    

private:
    DiskCDAG *cdag;
    unsigned int cacheSize;
    unsigned int neighborPriorityCount;
    unsigned int successorPriorityCount;
    bool selectNeighborFlag; // a flag marking if we try select neighbor or successor in 
    ofstream outFile;
    
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
    boost::unordered_map<Id, boost::array<Id, 2> > nodeIdToUnprocPredsSuccsCountMap;

    size_t numNodes;
};

unsigned int ConvexPartitioning::ConvexComponent::tileId = 0;
}