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
#include "CalculatePebbleCost.hxx"
#include "MacroDAG.hxx"

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
        if(mergedNodesBitSet)
        {
            delete []mergedNodesBitSet;
        }
        if(cdagOrigCDAG)
        {
            delete cdagOrigCDAG;
            cdagOrigCDAG = 0;
        }
        if(cdagMacroCDAG)
        {
            delete cdagMacroCDAG;
            cdagMacroCDAG = 0;
        }
        if(macroNodeCache)
        {
            delete macroNodeCache;
            macroNodeCache = 0;
        }

        if(mDag)
        {
            delete mDag;
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
            takenNeighbors = 0;
            takenSuccs = 0;
            readyNeighborsList.clear();
            readySuccsList.clear();
            nodesList.clear();
            //idToNeighborsListMap.clear();
            //idToSuccsListMap.clear();
            ++ConvexComponent::tileId;
        }

        void writeNodesOfComponent(ostream &out, DiskCDAG<GraphNode> *dag)
        {
            // sort nodes based on their dynamic ids
            sort(nodesList.begin(), nodesList.end());
            out << "Tile " << ConvexComponent::tileId;
            for(deque<Id>::iterator it = nodesList.begin();
                it != nodesList.end(); ++it)
            {
                GraphNode *temp = dag->getNode(*it);
                out << "\nStatic ID: " << temp->staticId << ";";
                out << "Dyn ID: " << temp->dynId << ";";
                out << " " << llvm::Instruction::getOpcodeName(temp->type) << ";";
            }
            out << "\n";
            out << flush;
        }

        void print()
        {
            cout << "\n";
            cout << "Printing convex component : " <<ConvexComponent::tileId;
            cout << "\n Nodes list: ";
            for(deque<Id>::iterator it = nodesList.begin();
                it != nodesList.end(); ++it)
            {
                cout << *it << " ";
            }

            cout << "\nReady Neighbors: ";
            for(deque<Id>::iterator it = readyNeighborsList.begin();
                it != readyNeighborsList.end(); ++it)
            {
                cout << *it << " ";
            }
            cout << "\nReady Successor: ";
            for(deque<Id>::iterator it = readySuccsList.begin();
                it != readySuccsList.end(); ++it)
            {
                cout << *it << " ";
            }
            cout << "\n takenNeighbors : " << takenNeighbors;
            cout << " takenSuccs : " << takenSuccs;
            cout << "\n";
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
        cdagOrigCDAG = DiskCDAG<GraphNode>::generateGraph<GraphNode, DiskCDAGBuilder<GraphNode> >(ids, programName); 
        clock_t end = clock();
        double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
        cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;
        readyNodesBitSet = 0;
        mergedNodesBitSet = 0;

        if(cdagOrigCDAG)
        {
            NUM_SLOTS = cdagOrigCDAG->getNumSlots();
            BLOCK_SIZE = cdagOrigCDAG->getBlockSize();
            //cdagOrigCDAG->printDOTGraph("diskgraph.dot");
            //cdagOrigCDAG->createMacroGraph();
            //cdagOrigCDAG->printDOTGraph("diskgraphwithmacronodes.dot");
            numNodes = cdagOrigCDAG->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            writeOriginalMemTrace();
            writeOriginalMemTraceWithPool();

            cout <<"\n Generating Macro CDAG..." << flush;
            mergedNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            writeMacroNodeInformation();

            // Switch to MACRO cdagOrigCDAG
            //delete cdagOrigCDAG;
            cdagMacroCDAG = DiskCDAG<GraphNode>::generateGraphUsingFile<GraphNode>("diskmacrograph");
            ofstream outmacro("diskmacrographfile");
            cdagMacroCDAG->printDiskGraph(outmacro);

            numNodes = cdagMacroCDAG->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            cout <<"done!" << flush;

            readyNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];

            cdagMacroCDAG->initNeighborCache();
            // ofstream out("diskgraphwithneighborinfo");
            // cdagMacroCDAG->printDiskGraph(out);  
        }
        else
        {
          std::cerr << "Fatal : Failed to generate cdagOrigCDAG \n";
        }
    }

    bool canBeMerged(Id &curNodeId, Address &succAddr)
    {
        return (cdagOrigCDAG->getNode(curNodeId)->succsList.size() == 1  
                && cdagOrigCDAG->getNode(curNodeId)->type != Instruction::Load
                && (cdagOrigCDAG->getNode(curNodeId)->addr < maxStaticId
                    || cdagOrigCDAG->getNode(curNodeId)->addr == succAddr
                    )
                );
    }  

    void createMacroNode(Id &curNodeId, deque<Id> &microNodeList)
    {
        Id predCount = cdagOrigCDAG->getNode(curNodeId)->predsList.size();
        for(Id i=0; i<predCount; ++i)
        {
            // If predecessor is special node then it can 
            // be added to the macro node
            Id predId = cdagOrigCDAG->getNode(curNodeId)->predsList[i];
            if(canBeMerged(predId, cdagOrigCDAG->getNode(curNodeId)->addr))
            {
                utils::setBitInBitset(mergedNodesBitSet, predId, numOfBytesForReadyNodeBitSet);
                microNodeList.push_back(predId);
                createMacroNode(predId, microNodeList);
            }
        }
    }

    void writeMacroNodeInformation()
    {
        memset(mergedNodesBitSet, 0, numOfBytesForReadyNodeBitSet);
        BYTE *visitedNodeBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
        memset(visitedNodeBitSet, 0, numOfBytesForReadyNodeBitSet);
        string macroNodeListFileName = "diskgraphmacronodeinfo";

        ofstream macroNodeListFile(macroNodeListFileName.c_str(), fstream::binary);
        deque<Id> microNodeList;
        Id macroGraphSize = numNodes;

        for(Id i=numNodes-1; i >= 0; --i)
        {
            microNodeList.clear();
            if(!utils::isBitSet(visitedNodeBitSet, i, numOfBytesForReadyNodeBitSet))
            {
                createMacroNode(i, microNodeList);
            }
            macroGraphSize -= microNodeList.size();

            // Write Macro node to a file
            macroNodeListFile.write((const char*)&i, sizeof(Id));
            Id microNodesCount = microNodeList.size();
            macroNodeListFile.write((const char*)&microNodesCount, sizeof(Id));
            for(Id j=0; j<microNodesCount; ++j)
            {
                macroNodeListFile.write((const char*)&microNodeList[j], sizeof(Id));
                // Also mark these nodes as visited
                utils::setBitInBitset(visitedNodeBitSet, microNodeList[j], numOfBytesForReadyNodeBitSet);
            }
        }
        macroNodeListFile.close();
        delete []visitedNodeBitSet;
        // Initialize the macro node cache
        macroNodeCache = new DiskCache<DataList, Id>(1024, 4);
        if(!macroNodeCache->init(macroNodeListFileName))
        {
            cout << "\n Cache initialization failed for macro nodes list...";
            return;
        }

        // Test Code to print macro nodes
        // cout << "\n Printing macro node information...";
        // for(Id i=0; i<numNodes; ++i)
        // {
        //     macroNodeCache->getData(i)->print(cout);
        // }
        // cout << "\nNum of macro nodes  = " << macroGraphSize;

        writeMacroGraph(macroGraphSize);
    }

    void writeMacroGraph(Id macroGraphSize)
    {
        mDag = new MacroDAG();
        mDag->generateMacroDAG(cdagOrigCDAG, macroNodeCache, mergedNodesBitSet, macroGraphSize);
        // mDag->printMacroNodeToOrigNodeMap();
    }

    void generateConvexComponents(unsigned int cacheS,
                                    unsigned int nPC,
                                    unsigned int sPC)
    {
        
        ofstream cur_proc_node("cur_proc_node");
        cacheSize = cacheS;
        neighborPriorityCount = nPC;
        successorPriorityCount = sPC;
        selectNeighborFlag = neighborPriorityCount >= successorPriorityCount ? true : false;

        idToNeighborsListMap = new bool[numNodes];
        memset(idToNeighborsListMap, 0, sizeof(bool)*numNodes);
        idToSuccsListMap = new bool[numNodes];
        memset(idToSuccsListMap, 0, sizeof(bool)*numNodes);


        prepareInitialReadyNodes();
        cout <<"\n Initial ready node count = " << readyNodeCount;

        outFile.open("convex_out_file");

        ofstream actualScheduleFile("actual_schedule");

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
            nodeId = selectReadyNode(empQ);
            if(empQ)
            {
                cout << "\n Ready Node Count : " << readyNodeCount;
                cout << "But ready node queue is empty!...exiting";
                cout << flush;
                break;
            }
            GraphNode *curNode = cdagMacroCDAG->getNode(nodeId);
            //cout << "\n\nSelected node (from selectReadyNode): " << nodeId;

            // we have valid nodeId for a ready node
            curConvexComp.reset();
            memset(idToNeighborsListMap, 0, sizeof(bool)*numNodes);
            memset(idToSuccsListMap, 0, sizeof(bool)*numNodes);
            liveSet.clear();
            firstPass = true;
            //cout << "\nConvex component with id : " << ConvexComponent::tileId;

            clock_t beg = clock();

            // The ready node is already selected in the variable 'node'
            while(readyNodeCount > 0 && updateLiveSet(liveSet, curNode, liveSetSize))
            {
                cur_proc_node << curNode->getId() << " (";
                cur_proc_node << double(clock() - beg) / (CLOCKS_PER_SEC * 60);
                cur_proc_node << ")";
                
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
                for(Id i=0; i<curNode->predsList.size(); ++i)
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
                    // cout << "\n0 selected best node";
                    // curConvexComp.print();
                    break;
                }
                else
                {
                    if(curNode->getId() == nodeId)
                    {
                        //cout << "\n Error selected the same node...";
                        return;
                    }
                    curNode = cdagMacroCDAG->getNode(nodeId);
                }
            }
            // Now the current convex component should have at
            // least one node.
            // If not, then that means that the liveSet of the
            // current selected node is higher than C
            // In such cases, pop this node and add it to the
            // end of the queue
            // Since its a DAG, this should not result in a 
            // infinite loop.
            if(curConvexComp.nodesList.size() > 0)
            {
                //convexComponents.push_back(curConvexComp.nodesList);                
                deque<Id> expandedConvexComp;
                for(Id i=0; i < curConvexComp.nodesList.size(); ++i)
                {
                    // if we added a MACRO node to CC, add all 
                    // other nodes in that node first
                    Id origNodeId = mDag->getOrigCDAGNodeId(curConvexComp.nodesList[i]);
                    Id microNodesCount = macroNodeCache->getData(origNodeId)->list.size();
                    sort(macroNodeCache->getData(origNodeId)->list.begin(), 
                        macroNodeCache->getData(origNodeId)->list.end());
                    for(Id j=0; j<microNodesCount; ++j)
                    {
                        expandedConvexComp.push_back(macroNodeCache->getData(origNodeId)->list[j]);
                    }
                    // and then the current node
                    expandedConvexComp.push_back(origNodeId);
                }
                convexComponents.push_back(expandedConvexComp);
                //actualScheduleFile << "Tile " << ConvexComponent::tileId;
                for(deque<Id>::iterator it = expandedConvexComp.begin();
                    it != expandedConvexComp.end(); ++it)
                {
                    GraphNode *temp = cdagOrigCDAG->getNode(*it);
                    //actualScheduleFile << "\nStatic ID: " << temp->staticId << ";";
                    //actualScheduleFile << "Dyn ID: " << temp->dynId << ";";
                    //actualScheduleFile << " " << llvm::Instruction::getOpcodeName(temp->type) << ";";
                }
                //actualScheduleFile << "\n";
                //actualScheduleFile << flush;
                //curConvexComp.writeNodesOfComponent(outFile, cdagMacroCDAG);
                //outFile << "live set size  = " << liveSet.size() << "\n";
                //cout << "\n end of one convex component. live set size = " <<liveSet.size();
                processedNodeCount += curConvexComp.nodesList.size();
            }
            else
            {
                //outFile << "\n Ready node :" << nodeId;
                //outFile << ", Live set size : " << liveSetSize;
                //outFile << "\n Exceeds the specified Max Live value of " << cacheSize;
            }
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
        
        // Perform cleanup to free some memory
        remove("cur_proc_node");
        delete []idToNeighborsListMap;
        delete []idToSuccsListMap;
        nodeIdToUnprocPredsSuccsCountMap.clear();
        if(macroNodeCache)
        {
            delete macroNodeCache;
            macroNodeCache = 0;
        }

        // switch back to original CDAG
        numNodes = cdagOrigCDAG->getNumNodes();
        numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);

        cout << "\n Done with the heuristics...switching to original CDAG." << flush;
        if(cdagMacroCDAG)
        {
            delete cdagMacroCDAG;
            cdagMacroCDAG = 0;
            cout << "\n Done deleting the macro cdag";
        }
    }

    void printDeque(deque<Id> &mydeque, string qname)
    {
        cout <<"\n"<<qname << ":";
        for (std::deque<Id>::iterator it = mydeque.begin(); it!=mydeque.end(); ++it)
            std::cout << ' ' << *it;
    }

    Id selectBestNode(ConvexComponent &cc,
        GraphNode *curNode)
    {
        //cout <<"\nIn selectBestNode...";
        Id nextNodeId = 0;
        // Update readyNeighborsList and readySuccsList
        // i.e. neighbors(curNode) INTERSECTION ReadyNodeSet
        // neighbors(curNode) is union of all the predecessors of curNode successors
        Id succCount = curNode->succsList.size();
        
        NodeNeighborInfo *neighbor = cdagMacroCDAG->getNeighbor(curNode->dynId);
        Id neighborCount = neighbor->neighborList.size();
        for(Id i=0; i<neighborCount; ++i)
        {
            if(isReady(neighbor->neighborList[i])  
                && !idToNeighborsListMap[neighbor->neighborList[i]]                
                )
            {
                // this neighbor can now be added to the ready neighbor list
                cc.readyNeighborsList.push_back(neighbor->neighborList[i]);
                idToNeighborsListMap[neighbor->neighborList[i]] = true;
                //cout << "\n Adding neighbor to ready list with id : " << neighbor->neighborList[i];
            }
        }

        for(Id i=0; i<succCount; ++i)
        {
            // Successor List update
            if(isReady(curNode->succsList[i])
                && !idToSuccsListMap[curNode->succsList[i]]                
                )
            {
                // this successor can now be added to ready successor list
                cc.readySuccsList.push_back(curNode->succsList[i]);
                idToSuccsListMap[curNode->succsList[i]] = true;
                //cout << "\n Adding successor to ready list with id : " << curNode->succsList[i];
            }
        }

        // Remove curNode from ready lists
        //cout << "\nRemove node from ready lists : " << curNode->getId();
        if(idToNeighborsListMap[curNode->getId()])
        {
            //cout <<"\n Removing from neighbors list...";
            idToNeighborsListMap[curNode->getId()] = false;
            //printDeque(cc.readyNeighborsList, "n-list");
        }
        if(idToSuccsListMap[curNode->getId()])
        {
            //cout <<"\n Removing from successors list...";            
            idToSuccsListMap[curNode->getId()] = false;
            //printDeque(cc.readySuccsList, "s-list");
        }

        // Removing dead nodes from neigbhors and successor list
        while(!cc.readyNeighborsList.empty() && 
            !idToNeighborsListMap[cc.readyNeighborsList.front()])
        {
            //cout << "\n Removing n-list : " <<cc.readyNeighborsList.front();
            cc.readyNeighborsList.pop_front();
        }
        while(!cc.readySuccsList.empty() && 
            !idToSuccsListMap[cc.readySuccsList.front()])
        {
            //cout <<"\n Removing s-list : " << cc.readySuccsList.front();
            cc.readySuccsList.pop_front();
        }

        // printDeque(cc.readyNeighborsList, "n-list");
        // printDeque(cc.readySuccsList, "s-list");
        // cout << "\n Before ==> takenNeighbors : " <<cc.takenNeighbors << " takenSuccs : " << cc.takenSuccs;

        // Determine the next ready node to be returned
        //cout << "\n";
        //cout << "Neighbors turn ? " << selectNeighborFlag;
        //cout << " readyNeighborsList size : " <<  cc.readyNeighborsList.size();
        //cout << " readySuccsList size : " << cc.readySuccsList.size();
        //cout << " takenNeighbors : " << cc.takenNeighbors;
        //cout << " takenSuccs : " << cc.takenSuccs;
        if(cc.readyNeighborsList.size() > 0 || cc.readySuccsList.size() > 0)
        {
            /* A different heuristic implementation than the original one*/

            // // Choose between a neighbor or a successor depending on the
            // // priority
            // if(selectNeighborFlag) 
            // {
            //     //cout << "\nNeighbor turn...";
            //     // It's ready neighbors turn...
            //     if(cc.readyNeighborsList.size() > 0)
            //     {
            //         // ... and there are neighbors.

            //         // no need to pop and erase the next node here
            //         // because in next call this will be erased
            //         // or if there is no next call then this data structure
            //         // will be reset before use.
            //         nextNodeId = cc.readyNeighborsList.front();
            //         ++cc.takenNeighbors;
            //         if(cc.takenNeighbors >= neighborPriorityCount)
            //         {
            //             cc.takenNeighbors = 0;
            //             selectNeighborFlag = false;
            //         }
            //         //cout << "selected neighbor with id : " <<nextNodeId;
            //     }
            //     else
            //     {
            //         // but we ran out of neighbors..return ready successors
            //         // till we get a neighbor.
            //         // Do not increment takenSuccs count.
            //         nextNodeId = cc.readySuccsList.front();
            //         //cout << "selected successor with id : " <<nextNodeId;
            //     }
            // }
            // else 
            // {
            //     //cout <<"\nSuccessor turn...";
            //     // It's ready successors turn...
            //     if(cc.readySuccsList.size() > 0)
            //     {
            //         // ... and there are successors
            //         nextNodeId = cc.readySuccsList.front();
            //         ++cc.takenSuccs;
            //         if(cc.takenSuccs >= successorPriorityCount)
            //         {
            //             cc.takenSuccs = 0;
            //             selectNeighborFlag = true;
            //         }
            //         //cout << "selected successor with id : "<<nextNodeId;
            //     }
            //     else
            //     {
            //         // but we ran out of successors..return ready neighbors
            //         // till we get a successor.
            //         // Do not increment takenNeighbors count.
            //         nextNodeId = cc.readyNeighborsList.front();
            //         //cout << "selected neighbor with id : " <<nextNodeId;
            //     }
            // }

            /* Exactly matches the originally implemented heuristic */
            
            // cout << "\n";
            //cout << "\n takenNeighbors : " <<cc.takenNeighbors;
            //cout << " takenSuccs : " << cc.takenSuccs;
            if(cc.takenNeighbors == neighborPriorityCount && 
                cc.takenSuccs == successorPriorityCount)
            {
                cc.takenNeighbors = 0;
                cc.takenSuccs = 0;
            }

            if(cc.takenSuccs < successorPriorityCount)
            {
                if(cc.readySuccsList.size() > 0)
                {
                    nextNodeId = cc.readySuccsList.front();
                    ++cc.takenSuccs;
                    // cout << "\nselected successor with id : "<<nextNodeId;
                }
                else
                {
                    nextNodeId = cc.readyNeighborsList.front();
                    ++cc.takenNeighbors;
                    // cout << "\nselected neighbor with id : " <<nextNodeId;
                }
            }
            else if(cc.takenNeighbors < neighborPriorityCount)
            {
                if(cc.readyNeighborsList.size() > 0)
                {
                    nextNodeId = cc.readyNeighborsList.front();
                    ++cc.takenNeighbors;
                    // cout << "\nselected neighbor with id : "<<nextNodeId;
                }
                else
                {
                    nextNodeId = cc.readySuccsList.front();
                    ++cc.takenSuccs;
                    // cout << "\nselected successor with id : " <<nextNodeId;
                }
            }
        }
        else
        {
            // There are no ready neighbors or successors just yet
            // get the next ready node
            // nextNodeId = selectReadyNode();
        }
        // cout << "\n After ==> takenNeighbors : " <<cc.takenNeighbors << " takenSuccs : " << cc.takenSuccs;
        return nextNodeId;
    }

    bool updateLiveSet(set<Id> &liveSet, GraphNode *curNode, 
        unsigned int &lsSize)
    {
        Id curNodeId = curNode->getId();
        //cout << "\nIn updateLiveSet...";
        bool retVal = true;
        set<Id> oldLiveSet = liveSet;

        // If n has some unprocessed successor then add n 
        // to the live set.
        if(nodeIdToUnprocPredsSuccsCountMap[curNode->getId()][SUCC_INDEX] > 0)
        {
            liveSet.insert(curNode->getId());
        }

        // Resurrecting a predecessor node or killing it if curNode was 
        // the last unprocessed successor for this predecessor
        Id predCount = curNode->predsList.size();
        for(Id i=0; i<predCount; ++i)
        {
            if(nodeIdToUnprocPredsSuccsCountMap[curNode->predsList[i]][SUCC_INDEX] == 1)
            {
                // it is the last unprocessed successor of this predecessor
                liveSet.erase(curNode->predsList[i]);
            }
            else
            {
                liveSet.insert(curNode->predsList[i]);
            }
        }
        lsSize = liveSet.size();

        // Compare if we haven't exceeded the tile size
        if(liveSet.size() > cacheSize)
        {
            liveSet = oldLiveSet;
            retVal = false;
        }
        //cout << "\nExiting updateLiveSet with retVal = " << retVal;
        return retVal;
    }

    void updateListOfReadyNodes(GraphNode *node)
    {
        Id succCount = node->succsList.size();
        for(Id i=0; i<succCount; ++i)
        {
            // If this successor has no more unprocessed predecessor 
            // then its ready

            //GraphNode *succNode = node->succsList[i];
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

    void prepareInitialReadyNodes()
    {
        // First set all the bits i.e. mark all nodes as 'not' ready
        memset(readyNodesBitSet, ~0, numOfBytesForReadyNodeBitSet);

        // Traverse over the graph once to mark all the nodes as ready
        // that have 0 preds i.e. all input vertices
        nodeIdToUnprocPredsSuccsCountMap.reserve(numNodes);
        for(Id i=0; i<numNodes; ++i)
        {
            GraphNode *node = cdagMacroCDAG->getNode(i);
            if(node->predsList.size() == 0)
            {
                // a node with zero predecessors
                // it should be the load node
                assert(node->type == 27); 
                // mark this node as ready i.e. unset the corresponding bit
                utils::unsetBitInBitset(readyNodesBitSet, i, numOfBytesForReadyNodeBitSet);
                readyNodeQ.push_back(i);
                ++readyNodeCount;
            }
            boost::array<Id,2> temp = {{node->predsList.size(), node->succsList.size()}};
            nodeIdToUnprocPredsSuccsCountMap.push_back(temp);
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

    DiskCDAG<GraphNode>* getCdagPtr()
    {
        return cdagOrigCDAG;
    }

    void writeOriginalMemTrace()
    {
        ofstream origMemTraceFile("original_memtrace.txt");
        for(Id i=0; i<numNodes; ++i)
        {
            GraphNode *curNode = cdagOrigCDAG->getNode(i);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(Id j=0; j < predCount; ++j)
                {
                    GraphNode *predNode = cdagOrigCDAG->getNode(curNode->predsList[j]);
                    origMemTraceFile << predNode->addr << "\n";
                }
                origMemTraceFile << curNode->addr << "\n";
            }
        }
    }

    void assignAddr(Address *addr, deque<Address> &freeAddr, Address &nextAddr)
    {
        //*addr = nextAddr++;
        //return;
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
            remUses[i] = cdagOrigCDAG->getNode(i)->succsList.size();
        }
        deque<Address> freeAddr;
        Address nextAddr = 1;
        ofstream memTraceFile("orig_memtrace_withpool.txt");
        int numOfCC = convexComponents.size();
        for(Id i=0; i<numNodes; ++i)
        {
            GraphNode *curNode = cdagOrigCDAG->getNode(i);
            size_t currId = curNode->dynId;
            assert(currId < numNodes);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(Id k=0; k < predCount; ++k)
                {
                    GraphNode *predNode = cdagOrigCDAG->getNode(curNode->predsList[k]);
                    Id pred = predNode->dynId;
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
        cout << "\n Writing memtrace for schedule with pool..." <<flush;
        Address *addr = new Address[numNodes]();
        int *remUses = new int[numNodes];
        for(Id i=0; i<numNodes; ++i)
        {
            remUses[i] = cdagOrigCDAG->getNode(i)->succsList.size();
        }
        deque<Address> freeAddr;
        Address nextAddr = 1;
        ofstream memTraceFile("memtrace_withpool.txt");
        Id numOfCC = convexComponents.size();
        for(Id i=0; i<numOfCC; ++i)
        {
            Id numNodesInCC = convexComponents[i].size();
            for(Id j=0; j<numNodesInCC; ++j)
            {
                GraphNode *curNode = cdagOrigCDAG->getNode(convexComponents[i][j]);
                size_t currId = curNode->dynId;
                assert(currId < numNodes);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(Id k=0; k < predCount; ++k)
                    {
                        Id pred = curNode->predsList[k];
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
        cout << "done!" << flush;

        delete []addr;
        delete []remUses;
    }

    void writeMemTraceForSchedule()
    {
        cout << "\n Writing memtrace for schedule without pool..." <<flush;
        ofstream memTraceFile("memtrace.txt");
        ofstream tempMemTraceFileWrite("temp_memtrace.txt", ios::binary);
        Id numOfCC = convexComponents.size();

        // Optimization trick :  Write the Ids first and then
        // do a second pass to fetch corresponding addresses.
        // Otherwise two calls were need to the cache : first
        // to fetch the current node and then to fetch its predecessors.
        for(Id i=0; i<numOfCC; ++i)
        {
            Id numNodesInCC = convexComponents[i].size();
            for(Id j=0; j<numNodesInCC; ++j)
            {
                GraphNode *curNode = cdagOrigCDAG->getNode(convexComponents[i][j]);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(Id k=0; k < predCount; ++k)
                    {                        
                        tempMemTraceFileWrite.write((const char*)&curNode->predsList[k], sizeof(Id));
                    }
                    tempMemTraceFileWrite.write((const char*)&curNode->dynId, sizeof(Id));
                }
            }
        }

        tempMemTraceFileWrite.close();
        ifstream tempMemTraceFileRead("temp_memtrace.txt");
        Id curId = 0;
        while(tempMemTraceFileRead.read((char*)&curId, sizeof(Id)))
        {
            memTraceFile << cdagOrigCDAG->getNode(curId)->addr << "\n";
        }

        remove("temp_memtrace.txt");
        cout << "done!" << flush;
    }

    public:
    void calculateCost()
    {
        Id *arrForCost = new Id[numNodes]();
        Id numOfCC = convexComponents.size();
        Id count = 0;
        // unsigned int K[] = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000};
        unsigned int K[] = {10, 20, 50, 100, 500, 1000};
        int ele = 6;

        cout << "\n Calculating cost for original schedule..."<<flush;

        for(Id i=0; i<numNodes; ++i)
        {
            arrForCost[count++] = i;
        }
        
        assert(count == numNodes);
        for(int i=0; i < ele; ++i)
        {
            int cost  = calcPblCost< DiskCDAG<GraphNode> >(cdagOrigCDAG, arrForCost, K[i],  numNodes);
            cout << "\n" << K[i] << " : " << cost;
        }


        cout << "\n Calculating cost for new schedule..." << flush;
        count = 0;

        for(Id i=0; i<numOfCC; ++i)
        {
            Id numNodesInCC = convexComponents[i].size();
            for(Id j=0; j<numNodesInCC; ++j)
            {
                arrForCost[count++] = convexComponents[i][j];
            }
        }
        assert(count == numNodes);
        for(int i=0; i < ele; ++i)
        {
            int cost  = calcPblCost< DiskCDAG<GraphNode> >(cdagOrigCDAG, arrForCost, K[i],  numNodes);
            cout << "\n" << K[i] << " : " << cost << flush;
        }

        delete []arrForCost;
    }

    

private:
    DiskCDAG<GraphNode> *cdagOrigCDAG;
    DiskCDAG<GraphNode> *cdagMacroCDAG;
    unsigned int cacheSize;
    unsigned int neighborPriorityCount;
    unsigned int successorPriorityCount;
    unsigned int NUM_SLOTS;
    unsigned int BLOCK_SIZE;
    bool selectNeighborFlag; // a flag marking if we try select neighbor or successor in 
    ofstream outFile;
    
    deque< deque<Id> > convexComponents;

    size_t numOfBytesForReadyNodeBitSet;
    BYTE *readyNodesBitSet; // a bit set for quick lookup
    BYTE *mergedNodesBitSet;
    Id readyNodeCount;
    unsigned int bitSetIndex;
    //boost::unordered_map<Id, bool>; // a map for quick lookup

    // Implementation Note :
    // Assuming 8 byte for Id, this map for about 20 million nodes
    // will occupy roughly 457 MB of memory. If its expected to 
    // process more nodes then we should be writing and reading this
    // map from the file by making use of the DiskCache.
    //boost::unordered_map<Id, boost::array<Id, 2> > nodeIdToUnprocPredsSuccsCountMap;
    vector<boost::array<Id, 2> > nodeIdToUnprocPredsSuccsCountMap;

    deque<Id> readyNodeQ;

    bool *idToNeighborsListMap;
    bool *idToSuccsListMap;
    Id maxStaticId;

    size_t numNodes;
    DiskCache<DataList, Id> *macroNodeCache;
    MacroDAG *mDag;
};

unsigned int ConvexPartitioning::ConvexComponent::tileId = 0;
}