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
        cdag = DiskCDAG<GraphNode>::generateGraph<GraphNode, DiskCDAGBuilder<GraphNode> >(ids, programName); 
        clock_t end = clock();
        double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
        cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;
        readyNodesBitSet = 0;

        if(cdag)
        {
            cdag->printDOTGraph("diskgraph.dot");
            //cdag->createMacroGraph();
            //cdag->printDOTGraph("diskgraphwithmacronodes.dot");
            numNodes = cdag->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            readyNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            
            writeOriginalMemTrace();
            writeOriginalMemTraceWithPool();

            cdag->initNeighborCache();
            //ofstream out("diskgraphwithneighborinfo");
            //cdag->printDiskGraph(out);

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

    // bool checkChaining(deque<Id> &tempPartition, Id &curNodeId)
    // {
    //     bool retVal = true;        

    //     tempPartition.push_back(curNodeId);
    //     cout << "\n Trying to perform chaining for :" << curNodeId;

    //     Id tempNodeId = cdag->getNode(curNodeId)->succsList[0];
    //     while(isSpecialNode(tempNodeId))
    //     {
        
    //         // Go to the next node and check for condition:
    //         // The only successor it has, is having just one 
    //         // unprocessed predecessor i.e. it can be ready if the 
    //         // current node is processed.
        
    //         if (!isSpecialChain(tempNodeId))
    //         {
    //                 // Its not ready to be part of the chain just yet
    //             return false;
    //         }
    //         tempPartition.push_back(tempNodeId);
    //         tempNodeId = cdag->getNode(tempNodeId)->succsList[0];
    //     }

    //     tempPartition.push_back(tempNodeId);
    //     return retVal;
    // }

    bool checkChaining(deque<Id> &tempPartition, Id &curNodeId)
    {
        bool retVal = true;        

        tempPartition.push_back(curNodeId);
        cout << "\n Trying to perform chaining for :" << curNodeId;

        Id tempNodeId = cdag->getNode(curNodeId)->succsList[0];
        Id prevId =0;
        while(isSpecialNode(tempNodeId))
        {
            tempPartition.push_back(tempNodeId);
            prevId = tempNodeId;
            tempNodeId = cdag->getNode(tempNodeId)->succsList[0];
            if(prevId ==  tempNodeId-1 && 
                nodeIdToUnprocPredsSuccsCountMap[tempNodeId][PRED_INDEX] == 1)
            {
                continue;
            }
            else
            {
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
        writeMacroNodeInformation();
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
            GraphNode *curNode = cdag->getNode(nodeId);
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
                
                // Check for chain of nodes here
                
                if(!isSpecialNode(nodeId))
                {
                    // add it current convex component
                    int microNodesCount = macroNodeCache->getData(nodeId)->list.size();
                    for(int i=0; i<microNodesCount; ++i)
                    {
                        curConvexComp.nodesList.push_back(macroNodeCache->getData(nodeId)->list[i]);
                    }
                    curConvexComp.nodesList.push_back(nodeId);
                }

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
                convexComponents.push_back(curConvexComp.nodesList);
                curConvexComp.writeNodesOfComponent(outFile, cdag);
                outFile << "live set size  = " << liveSet.size() << "\n";
            //cout << "\n end of one convex component. live set size = " <<liveSet.size();
                processedNodeCount += curConvexComp.nodesList.size();
            }
            else
            {
                outFile << "\n Ready node :" << nodeId;
                outFile << ", Live set size : " << liveSetSize;
                outFile << "\n Exceeds the specified Max Live value of " << cacheSize;
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
        remove("cur_proc_node");

    }

    /*void generateConvexComponents(unsigned int cacheS,
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

        ofstream prog("progress");
        int processedNodeCount = 0;
        int prev = -1;
        unsigned int liveSetSize = 0;
        bool firstPass = true;
        cout << "\n";
        
        Id nodeId = 0;

        ConvexComponent curConvexComp;
        set<Id> liveSet;
        Id prevNode = 0;
        bool updateReadyList = true;
        bool fromOuterLoop = true;
        Id prevSpecialNode = 0;


        while(readyNodeCount > 0)
        {
            bool empQ = false;
            nodeId = selectReadyNode(empQ);
            fromOuterLoop = true;
            updateReadyList = true;
            prevNode = nodeId;
            if(empQ)
            {
                cout << "\n Ready Node Count : " << readyNodeCount;
                cout << "But ready node queue is empty!...exiting";
                cout << flush;
                break;
            }
            GraphNode *curNode = cdag->getNode(nodeId);
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
                
                // Check for chain of nodes here
                deque<Id> tempPartition;
                if(isSpecialNode(nodeId))
                {
                    if(prevSpecialNode == nodeId)
                    {
                        break;
                    }
                    prevSpecialNode = nodeId;
                    if(checkChaining(tempPartition, nodeId))
                    {
                        cout << "\n Adding temp partition :";
                        for(deque<Id>::iterator it = tempPartition.begin();
                            it != tempPartition.end(); ++it)
                        {
                            nodeId = *it;
                            cout << nodeId << " ";
                            curConvexComp.nodesList.push_back(nodeId);
                            utils::setBitInBitset(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);
                            nodeIdToUnprocPredsSuccsCountMap[nodeId][PRED_INDEX] = -1;
                        }
                        tempPartition.clear();
                        --readyNodeCount; // just for the head of chain
                    }
                    else
                    {
                        // Its a node with a chain but the
                        // chain cannot be added now. 
                        // Reinsert it into the queue for later
                        // processing
                        readyNodeQ.push_back(nodeId);
                        cout << "\n Temp partition not added...";
                        if(fromOuterLoop)
                        {
                            cout << "came from outer loop..thus breaking.";
                            // then we can break and select next node
                            // from queue
                            break;
                        }
                        else
                        {
                            // this node was selected from selectBestNode()
                            // do not update the ready list
                            // and switch to previous node
                            cout << "selecting previous node :" << prevNode;
                            nodeId = prevNode;
                            curNode = cdag->getNode(prevNode);
                            updateReadyList = false;
                        }
                    }
                }
                else
                {
                    // add it current convex component
                    curConvexComp.nodesList.push_back(nodeId);

                    // set the bit for the node marking it processed
                    utils::setBitInBitset(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);
                    nodeIdToUnprocPredsSuccsCountMap[nodeId][PRED_INDEX] = -1;
                    --readyNodeCount;
                }


                if(updateReadyList)
                {

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
                }
                else
                {
                    updateReadyList = true; // reset it back
                }

                // Get the next ready node to add to the convex component
                prevNode = nodeId;
                nodeId = selectBestNode(curConvexComp, curNode);
                //cout << "\n\nSelected node (from selectBestNode): " << nodeId;
                if(nodeId == 0)
                {
                    fromOuterLoop = true;
                    break;
                }
                else
                {
                    if(curNode->getId() == nodeId)
                    {
                        //cout << "\n Error selected the same node...";
                        return;
                    }
                    fromOuterLoop = false;
                    curNode = cdag->getNode(nodeId);
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
                convexComponents.push_back(curConvexComp.nodesList);
                curConvexComp.writeNodesOfComponent(outFile, cdag);
                outFile << "live set size  = " << liveSet.size() << "\n";
            //cout << "\n end of one convex component. live set size = " <<liveSet.size();
                processedNodeCount += curConvexComp.nodesList.size();
            }
            else
            {
                outFile << "\n Ready node :" << nodeId;
                outFile << ", Live set size : " << liveSetSize;
                outFile << "\n Exceeds the specified Max Live value of " << cacheSize;
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
        remove("cur_proc_node");

    }*/

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

        NodeNeighborInfo *neighbor = cdag->getNeighbor(curNode->dynId);
        Id neighborCount = neighbor->neighborList.size();
        for(int i=0; i<neighborCount; ++i)
        {
            if(isReady(neighbor->neighborList[i]) && !idToNeighborsListMap[neighbor->neighborList[i]])
            {
                // this neighbor can now be added to the ready neighbor list
                cc.readyNeighborsList.push_back(neighbor->neighborList[i]);
                idToNeighborsListMap[neighbor->neighborList[i]] = true;
                //cc.idToNeighborsListMap.insert(neighbor->neighborList[i]);
                //cout << "\n Adding neighbor to ready list with id : " << succNode->predsList[j];
                //printDeque(cc.readyNeighborsList, "n-list");
            }
        }

        for(int i=0; i<succCount; ++i)
        {
            // GraphNode *succNode = cdag->getNode(curNode->succsList[i]);

            // // Neigbhor list update
            // Id succPredCount = succNode->predsList.size();
            // for(int j=0; j<succPredCount; ++j)
            // {
            //     // we have Id of a neighbor of curNode
            //     // check that this neighbor satisfies two condtions:
            //     // - its NOT in neigbors list of this component
            //     // - and its ready
            //     if(cc.idToNeighborsListMap.find(succNode->predsList[j]) == cc.idToNeighborsListMap.end()
            //         && isReady(succNode->predsList[j])
            //         )
            //     {
            //         // this neighbor can now be added to the ready neighbor list
            //         cc.readyNeighborsList.push_back(succNode->predsList[j]);
            //         cc.idToNeighborsListMap.insert(succNode->predsList[j]);
            //         //cout << "\n Adding neighbor to ready list with id : " << succNode->predsList[j];
            //         //printDeque(cc.readyNeighborsList, "n-list");
            //     }

            // }



            // Successor List update
            if(isReady(curNode->succsList[i]) && !idToSuccsListMap[curNode->succsList[i]])
            {
                // this successor can now be added to ready successor list
                cc.readySuccsList.push_back(curNode->succsList[i]);
                idToSuccsListMap[curNode->succsList[i]] = true;
                //cc.idToSuccsListMap.insert(curNode->succsList[i]);
                //cout << "\n Adding successor to ready list with id : " << curNode->succsList[i];
                //printDeque(cc.readySuccsList, "s-list");
            }
        }

        // Remove curNode from ready lists
        //cout << "\nRemove node from ready lists : " << curNode->getId();
        if(idToNeighborsListMap[curNode->getId()])
        {
            //cout <<"\n Removing from neighbors list...";
            //cc.readyNeighborsList.erase(find(cc.readyNeighborsList.begin(), cc.readyNeighborsList.end(), curNode->getId()));
            idToNeighborsListMap[curNode->getId()] = false;
            //cc.idToNeighborsListMap.erase(curNode->getId());
            //printDeque(cc.readyNeighborsList, "n-list");
        }
        if(idToSuccsListMap[curNode->getId()])
        {
            //cout <<"\n Removing from successors list...";
            //cc.readySuccsList.erase(find(cc.readySuccsList.begin(), cc.readySuccsList.end(), curNode->getId()));
            idToSuccsListMap[curNode->getId()] = false;
            //cc.idToSuccsListMap.erase(curNode->getId());
            //printDeque(cc.readySuccsList, "s-list");
        }

        // Removing dead nodes from neigbhors and successor list
        while(!cc.readyNeighborsList.empty() && 
            !idToNeighborsListMap[cc.readyNeighborsList.front()])
        {
            cc.readyNeighborsList.pop_front();
        }
        while(!cc.readySuccsList.empty() && 
            !idToSuccsListMap[cc.readySuccsList.front()])
        {
            cc.readySuccsList.pop_front();
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

    bool updateLiveSet(set<Id> &liveSet, GraphNode *curNode, 
        unsigned int &lsSize)
    {
        Id curNodeId = curNode->getId();
        if(isSpecialNode(curNodeId))
        {
            return true;
        }
        //cout << "\nIn updateLiveSet...";
        bool retVal = true;
        set<Id> oldLiveSet = liveSet;

        // If n has some unprocessed successor then add n 
        // to the live set.
        if(nodeIdToUnprocPredsSuccsCountMap[curNode->getId()][SUCC_INDEX] > 0)
        {
            liveSet.insert(curNode->getId());
        }

        // Birth of a successor node
        // Id succCount = curNode->succsList.size();
        // changedNodesList.reserve(succCount);
        // for(int i=0; i<succCount; ++i)
        // {
        //     // If this successor is still unprocessed add it to the live set
        //     Id succId = curNode->succsList[i];
        //     // if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] > 0)
        //     // {
        //     //     liveSet.insert(curNode->succsList[i]);
        //     //     //cout << "\nBirth of a successor node : " << curNode->succsList[i];
        //     // }

        //     // OPTIMIZATION NOTE:
             
        //         Collect all the nodes that have unprocessed predecessors
        //         as 0 after the current node is added to the partition. 
        //         These are the nodes that should be added to the ready
        //         list. This optimization trick helps in reducing the pass over
        //         successors list by one which is a huge gain if we are dealing
        //         with million nodes.
        //         Not a lot of nodes get ready in one call to this method.
            

        //     // First decrease the count by 1
        //     if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] > 0)
        //     {
        //         --nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX];
        //         if(nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] == 0)
        //         {
        //             toBeReadySuccNodesList.push_back(succId);
        //         }
        //         changedNodesList.push_back(succId);
        //     }

            
        // }

        // Resurrecting a predecessor node or killing it if curNode was 
        // the last unprocessed successor for this predecessor
        Id predCount = curNode->predsList.size();
        for(int i=0; i<predCount; ++i)
        {
            // bool hasAnyUnprocSuccs = false;
            // GraphNode *predNode = cdag->getNode(curNode->predsList[i]);
            // Id succCountForPredNode = predNode->succsList.size();
            // for(int j=0; j<succCountForPredNode; ++j)
            // {
            //     if(nodeIdToUnprocPredsSuccsCountMap[predNode->succsList[j]][PRED_INDEX] > 0)
            //     {
            //         // Yes this successor of the current predecessor is still unprocessed
            //         liveSet.insert(predNode->getId());
            //         hasAnyUnprocSuccs = true;
            //         //cout << "\nResurrecting node : " << predNode->getId();
            //         break;
            //     }
            // }
            // if(!hasAnyUnprocSuccs)
            // {
            //     // If all the successors of this predecessors are processed then we 
            //     // can remove this predecessor from the liveset
            //     liveSet.erase(predNode->getId());
            //     //cout << "\nRemoving died node : " << predNode->getId();
            // }

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
        int succCount = node->succsList.size();
        for(int i=0; i<succCount; ++i)
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
        for(int i=0; i<numNodes; ++i)
        {
            GraphNode *node = cdag->getNode(i);
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
        return cdag;
    }

    void writeOriginalMemTrace()
    {
        ofstream origMemTraceFile("original_memtrace.txt");
        for(int i=0; i<numNodes; ++i)
        {
            GraphNode *curNode = cdag->getNode(i);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(int j=0; j < predCount; ++j)
                {
                    GraphNode *predNode = cdag->getNode(curNode->predsList[j]);
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
            GraphNode *curNode = cdag->getNode(i);
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
                    GraphNode *predNode = cdag->getNode(curNode->predsList[k]);
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
                GraphNode *curNode = cdag->getNode(convexComponents[i][j]);
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
                        GraphNode *predNode = cdag->getNode(curNode->predsList[k]);
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
                GraphNode *curNode = cdag->getNode(convexComponents[i][j]);
                if(curNode->type == Instruction::Load)
                {
                    continue;
                }
                else
                {
                    Id predCount = curNode->predsList.size();
                    for(int k=0; k < predCount; ++k)
                    {
                        GraphNode *predNode = cdag->getNode(curNode->predsList[k]);
                        memTraceFile <<predNode->addr << "\n";
                    }
                    memTraceFile <<curNode->addr << "\n";
                }
            }
        }
    }

    

private:
    DiskCDAG<GraphNode> *cdag;
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
    //boost::unordered_map<Id, boost::array<Id, 2> > nodeIdToUnprocPredsSuccsCountMap;
    vector<boost::array<int, 2> > nodeIdToUnprocPredsSuccsCountMap;

    deque<Id> readyNodeQ;

    bool *idToNeighborsListMap;
    bool *idToSuccsListMap;
    Id maxStaticId;

    size_t numNodes;
    DiskCache<DataList, Id> *macroNodeCache;
};

unsigned int ConvexPartitioning::ConvexComponent::tileId = 0;
}