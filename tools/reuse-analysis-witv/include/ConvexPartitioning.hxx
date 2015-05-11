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
        tCount = 0;
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
        if(processedNodesBitSet)
        {
            delete []processedNodesBitSet;
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
        for(int i=0; i<ivMap.size(); ++i)
        {
            delete ivMap[i];
        }
    }

    struct ConvexComponent
    {
        static unsigned int tileId;
        deque<Id> nodesList;

        ConvexComponent()
        {
        }

        void reset()
        {
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
        cdagOrigCDAG = DiskCDAG<GraphNodeWithIV>::generateGraph<GraphNodeWithIV, DiskCDAGBuilderWithIV>(ids, programName); 
        clock_t end = clock();
        double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;
        cout << " \n\n Time taken to build the graph (in mins) : " << elapsed_time / 60;
        readyNodesBitSet = 0;
        processedNodesBitSet = 0;
        mergedNodesBitSet = 0;

        if(cdagOrigCDAG)
        {
            NUM_SLOTS = cdagOrigCDAG->getNumSlots();
            BLOCK_SIZE = cdagOrigCDAG->getBlockSize();
            //cdagOrigCDAG->printDOTGraph("diskgraph.dot");
            numNodes = cdagOrigCDAG->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            
            writeOriginalMemTrace();
            writeOriginalMemTraceWithPool();   

            cout <<"\n Generating Macro CDAG..." << flush;
            mergedNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            writeMacroNodeInformation();

            // Switch to MACRO cdagOrigCDAG
            cdagMacroCDAG = DiskCDAG<GraphNodeWithIV>::generateGraphUsingFile<GraphNodeWithIV>("diskmacrograph");
            ofstream outmacro("diskmacrographfile");            

            numNodes = cdagMacroCDAG->getNumNodes();
            numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
            cout <<"done!" << flush;

            readyNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            processedNodesBitSet = new BYTE[numOfBytesForReadyNodeBitSet];
            
                     
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
        //mDag->printMacroNodeToOrigNodeMap();
    }

    void generateConvexComponents(unsigned int T)
    {
        tCount = T;
        ofstream cur_proc_node("cur_proc_node");
        ofstream actualScheduleFile("actual_schedule");

        prepareInitialReadyNodes();
        cout <<"\n Initial ready node count = " << readyNodeCount;

        outFile.open("convex_out_file");

        ofstream prog("progress");
        int processedNodeCount = 0;
        int prev = -1;
        cout << "\n";
        
        Id nodeId = 0;

        ConvexComponent curConvexComp;

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
            markNodeAsProcessed(nodeId);

            updateListOfReadyNodes(cdagMacroCDAG->getNode(nodeId));

            // Check for all the iteration vectors

            // First find iv map index
            Id ivMapIndex =  nodeIdToIvMapIndexMap[nodeId];
            // IV map contains all IV in sorted format
            // So move forward in iv map till you hit the
            // limit of <i+T-1, j+T-1, k+T-1>
            vector<unsigned int> maxItVec(ivMap[ivMapIndex]->itVec);
            unsigned loopNum = 1;
            while(loopNum < maxItVec.size())
            {
                maxItVec[loopNum] += tCount-1;
                maxItVec[loopNum+1] += 
                loopNum += 2;
            }
            ++ivMapIndex;
            while(isValidIV(ivMapIndex, maxItVec))
            {
                NodeIVInfo *ivInfo = ivMap[ivMapIndex];
                if(!isNodeProcessed(ivInfo->nodeId))
                {
                    curConvexComp.nodesList.push_back(ivInfo->nodeId);
                    markNodeAsProcessed(ivInfo->nodeId);
                    // set the bit for the node marking it processed
                    //utils::setBitInBitset(readyNodesBitSet, ivInfo->nodeId, numOfBytesForReadyNodeBitSet);
                    //--readyNodeCount;

                    updateListOfReadyNodes(cdagMacroCDAG->getNode(ivInfo->nodeId));
                }
                ++ivMapIndex;
            }

            stack<Id> nodeStack;
            // Check for all the nodes in the current partition
            for(unsigned int j=0; j < curConvexComp.nodesList.size(); ++j)
            {
                Id tempId = curConvexComp.nodesList[j];
                unsigned int predCount = cdagMacroCDAG->getNode(tempId)->predsList.size();
                for(unsigned int p = 0; p < predCount; ++p)
                {
                    Id predNodeId = cdagMacroCDAG->getNode(tempId)->predsList[p];
                    if(!isNodeProcessed(predNodeId))
                    {
                        nodeStack.push(predNodeId);
                    }
                }
            }

            while(!nodeStack.empty())
            {
                Id tempId = nodeStack.top();
                if(!isNodeProcessed(tempId))
                {
                    curConvexComp.nodesList.push_back(tempId);
                    markNodeAsProcessed(tempId);
                    //utils::setBitInBitset(readyNodesBitSet, tempId, numOfBytesForReadyNodeBitSet);
                    //--readyNodeCount;
                    
                    
                    nodeStack.pop();

                    updateListOfReadyNodes(cdagMacroCDAG->getNode(tempId));
                    
                    unsigned int predCount = cdagMacroCDAG->getNode(tempId)->predsList.size();
                    for(unsigned int p = 0; p < predCount; ++p)
                    {
                        Id predNodeId = cdagMacroCDAG->getNode(tempId)->predsList[p];
                        if(!isNodeProcessed(predNodeId))
                        {
                            nodeStack.push(predNodeId);
                        }
                    }
                }
                else
                {
                    nodeStack.pop();
                }
            }

            //cout << "\nConvex component with id : " << ConvexComponent::tileId;

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
                actualScheduleFile << "Tile " << ConvexComponent::tileId;
                for(deque<Id>::iterator it = expandedConvexComp.begin();
                    it != expandedConvexComp.end(); ++it)
                {
                    GraphNode *temp = cdagOrigCDAG->getNode(*it);
                    actualScheduleFile << "\nStatic ID: " << temp->staticId << ";";
                    actualScheduleFile << "Dyn ID: " << temp->dynId << ";";
                    actualScheduleFile << " " << llvm::Instruction::getOpcodeName(temp->type) << ";";
                }
                actualScheduleFile << "\n";
                actualScheduleFile << flush;
                curConvexComp.writeNodesOfComponent(outFile, cdagMacroCDAG);
                //cout << "\n end of one convex component. live set size = " <<liveSet.size();
                processedNodeCount += curConvexComp.nodesList.size();
            }
            else
            {
                outFile << "\n Exceeds the specified Max Live value of ";
            }

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
        }
    }

    void printDeque(deque<Id> &mydeque, string qname)
    {
        cout <<"\n"<<qname << ":";
        for (std::deque<Id>::iterator it = mydeque.begin(); it!=mydeque.end(); ++it)
            std::cout << ' ' << *it;
    }

    void markNodeAsProcessed(Id &nodeId)
    {
        utils::setBitInBitset(processedNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);

        // If this node was not ready already don't decrease the
        // readyNodeCount
        if(isReady(nodeId))
        {
            // set the bit for the node marking it processed
            utils::setBitInBitset(readyNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);
            --readyNodeCount;
        }
    }

    bool isNodeProcessed(Id &nodeId)
    {
        return utils::isBitSet(processedNodesBitSet, nodeId, numOfBytesForReadyNodeBitSet);        
    }

    bool isValidIV(Id &ivMapIndex, vector<unsigned int> &maxItVec)
    {
        if(ivMapIndex >= ivMap.size())
        {
            return false;
        }
        bool retVal = true;
        for(int i =0; i<maxItVec.size(); ++i)
        {
            if(ivMap[ivMapIndex]->itVec[i] > maxItVec[i])
            {
                retVal = false;
            }
        }
        return retVal;
    }

    void updateListOfReadyNodes(GraphNodeWithIV *node)
    {
        Id succCount = node->succsList.size();
        for(Id i=0; i<succCount; ++i)
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
                if(!isNodeProcessed(succId) && nodeIdToUnprocPredsSuccsCountMap[succId][PRED_INDEX] == 0)
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
        memset(processedNodesBitSet, 0, numOfBytesForReadyNodeBitSet);

        // Traverse over the graph once to mark all the nodes as ready
        // that have 0 preds i.e. all input vertices
        nodeIdToUnprocPredsSuccsCountMap.reserve(numNodes);

        // Setup the IV map for fast lookup
        //unsigned int fixedLen = 2*(cdagOrigCDAG->maxDepth)+1;
        //cout << "\n Max Depth Value : " << cdagOrigCDAG->maxDepth;
        unsigned int fixedLen = 2*(7)+1;
        cout << "\n Max Depth Value : " << 7;
        
        ivMap.reserve(numNodes);
        for(Id i=0; i<numNodes; ++i)
        {
            NodeIVInfo *tempIVInfo = new NodeIVInfo(i, fixedLen);
            ivMap.push_back(tempIVInfo);
        }

        for(Id i=0; i<numNodes; ++i)
        {            
            // Store IV for this node in the map
            assert(ivMap[i]->itVec.size() >= cdagMacroCDAG->getNode(i)->loopItVec.size());
            for(int j=0; j<cdagMacroCDAG->getNode(i)->loopItVec.size(); ++j)
            {
                ivMap[i]->itVec[j+1] = cdagMacroCDAG->getNode(i)->loopItVec[j];
            }

            // Find all nodes ready for processing 
            if(cdagMacroCDAG->getNode(i)->predsList.size() == 0)
            {
                // a node with zero predecessors
                // it should be the load node
                assert(cdagMacroCDAG->getNode(i)->type == 27); 
                // mark this node as ready i.e. unset the corresponding bit
                utils::unsetBitInBitset(readyNodesBitSet, i, numOfBytesForReadyNodeBitSet);
                readyNodeQ.push_back(i);
                ++readyNodeCount;
            }
            boost::array<Id,1> temp = {{cdagMacroCDAG->getNode(i)->predsList.size()}};
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

        // Test code : Printing sorted IVs
        // cout << "\n Printing sorted Iteration vectors ";
        // for(int i=0; i < ivMap.size(); ++i)
        // {
        //     cout << "\n" << ivMap[i]->nodeId << " : ";
        //     for(int j=0; j < ivMap[i]->itVec.size(); ++j)
        //     {
        //         cout << ivMap[i]->itVec[j] << ",";
        //     }
        // }
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
        return cdagOrigCDAG;
    }

    void writeOriginalMemTrace()
    {
        ofstream origMemTraceFile("original_memtrace.txt");
        for(Id i=0; i<numNodes; ++i)
        {
            GraphNodeWithIV *curNode = cdagOrigCDAG->getNode(i);
            if(curNode->type == Instruction::Load)
            {
                continue;
            }
            else
            {
                Id predCount = curNode->predsList.size();
                for(Id j=0; j < predCount; ++j)
                {
                    GraphNodeWithIV *predNode = cdagOrigCDAG->getNode(curNode->predsList[j]);
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
            GraphNodeWithIV *curNode = cdagOrigCDAG->getNode(i);
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
                    GraphNodeWithIV *predNode = cdagOrigCDAG->getNode(curNode->predsList[k]);
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
                GraphNodeWithIV *curNode = cdagOrigCDAG->getNode(convexComponents[i][j]);
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
                GraphNodeWithIV *curNode = cdagOrigCDAG->getNode(convexComponents[i][j]);
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

    

private:
    DiskCDAG<GraphNodeWithIV> *cdagOrigCDAG;
    DiskCDAG<GraphNodeWithIV> *cdagMacroCDAG;
    unsigned int tCount;
    unsigned int NUM_SLOTS;
    unsigned int BLOCK_SIZE;
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
    BYTE *processedNodesBitSet; //  a bit set marking processed nodes
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
    vector<boost::array<Id, 1> > nodeIdToUnprocPredsSuccsCountMap;

    deque<Id> readyNodeQ;

    bool *idToNeighborsListMap;
    bool *idToSuccsListMap;
    Id maxStaticId;

    size_t numNodes;
    DiskCache<DataList, Id> *macroNodeCache;
    MacroDAG *mDag;

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