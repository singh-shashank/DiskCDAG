#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <ctime>

#include <boost/unordered_map.hpp>
#include <boost/array.hpp>
#include <boost/unordered_set.hpp>

#include "ddg/analysis/DiskCDAG.hxx"

using namespace std;

using namespace ddg;
using namespace llvm;
namespace ddg
{
class MacroDAG
{
public:
    void generateMacroDAG(DiskCDAG<GraphNodeWithIV> *cdag,
        DiskCache<DataList, Id> *macroNodeCache,
        BYTE *mergedNodesBitSet, Id macroGraphSize)
    {
        this->mergedNodesBitSet = mergedNodesBitSet;
        Id numNodes = cdag->getNumNodes();
        numOfBytesForReadyNodeBitSet = utils::convertNumNodesToBytes(numNodes);
        deque<Id> tempPredList;
        Id numMacroNodes = macroGraphSize;
        macroDagIdToCdagIdMap.reserve(numMacroNodes);
        cdagIdToMacroDagIdMap.reserve(numNodes);
        for(Id i=0; i < numNodes; ++i)
        {
            cdagIdToMacroDagIdMap.push_back(0);
        }

        for(Id i=0; i < numMacroNodes; ++i)
        {
            macroDagIdToCdagIdMap.push_back(0);
        }
        bool *visitedNode = new bool[numNodes];
        for(Id i=0; i < numNodes; ++i)
        {
            visitedNode[i] = false;
        }

        // cout << "\n " << macroDagIdToCdagIdMap.size() << " : " << cdagIdToMacroDagIdMap.size() << flush;
        Id curMacroNodeId = macroGraphSize-1;
        for(Id i=numNodes-1; i>=0; --i)
        {
            Id numMicroNodes = macroNodeCache->getData(i)->list.size();
            if(numMicroNodes > 0)
            {
                for(int j=0; j < numMicroNodes; ++j)
                {
                    Id curMicroNodeId = macroNodeCache->getData(i)->list[j];
                    visitedNode[curMicroNodeId] = true;
                    cdagIdToMacroDagIdMap[curMicroNodeId] = curMacroNodeId;
                }
                cdagIdToMacroDagIdMap[i] = curMacroNodeId;
                macroDagIdToCdagIdMap[curMacroNodeId] = i;
                --curMacroNodeId;
            }
            else
            {
                if(!visitedNode[i])
                {
                    cdagIdToMacroDagIdMap[i] = curMacroNodeId;
                    macroDagIdToCdagIdMap[curMacroNodeId] = i;
                    --curMacroNodeId;
                }
            }
            if(i==0) // Id is unsigned int so we cannot decrement it below 0
                break;
        }
        
        
        for(Id i=0; i < numMacroNodes; ++i)
        {
            nodePredList.push_back(tempPredList);
            //macroDagIdToCdagIdMap.push_back(0);
        }

        string tempMacroGraphFileName = "diskmacrographtemp";
        ofstream tempMacroGraphFileWrite(tempMacroGraphFileName.c_str());
        GraphNodeWithIV *temp = new GraphNodeWithIV();
        for(Id i=numNodes-1; i>=0; --i)
        {
            if(visitedNode[i])
            {
                //cout << "\n" << i << " is special node..continuing...";
                continue;
            }
            temp->reset();
            Id numMicroNodes = macroNodeCache->getData(i)->list.size();
            if(numMicroNodes <= 0)
            {
                //write the current node
                temp->deepCopy(cdag->getNode(i));
                temp->dynId = cdagIdToMacroDagIdMap[i];
                temp->succsList.clear();
                temp->predsList.clear();

                set<Id> uniqueSuccSet;
                for(Id j=0; j < cdag->getNode(i)->succsList.size(); ++j)
                {
                    uniqueSuccSet.insert(cdagIdToMacroDagIdMap[cdag->getNode(i)->succsList[j]]);
                    nodePredList[cdagIdToMacroDagIdMap[cdag->getNode(i)->succsList[j]]].push_back(temp->dynId);
                }
                for(set<Id>::iterator it = uniqueSuccSet.begin();
                    it != uniqueSuccSet.end(); ++it)
                {
                    temp->succsList.push_back(*it);
                }
                temp->writeToStreamInBinary(tempMacroGraphFileWrite);
            }
            else
            {
                set<Id> tempSet;
                temp->deepCopy(cdag->getNode(i));
                temp->dynId = cdagIdToMacroDagIdMap[i];
                temp->succsList.clear();
                temp->predsList.clear();

                // Go over the micro node list first
                for(Id j = 0; j < numMicroNodes; ++j)
                {
                    Id microNodeId = macroNodeCache->getData(i)->list[j];

                    Id succslistSize = cdag->getNode(microNodeId)->succsList.size();
                    for(Id k=0; k < succslistSize; ++k)
                    {
                        // It shouldn't be the special node because they have just
                        // one successor that lies within the macro node itself
                        // AND it don't add the current node as successor of itself.
                        if(!isSpecialNode(cdag->getNode(microNodeId)->succsList[k])
                            && cdag->getNode(microNodeId)->succsList[k] != i)
                        {
                            cout << "\n This should not happend while macro noding...";
                            tempSet.insert(cdagIdToMacroDagIdMap[cdag->getNode(microNodeId)->succsList[k]]);
                        }
                    }
                }
                // Check successors of the current node
                // cout << "\n Adding successors for : " << i << "\n";
                for(Id j=0; j < cdag->getNode(i)->succsList.size(); ++j)
                {
                    //if(!isSpecialNode(cdag->getNode(i)->succsList[j]))
                    {
                        // cout << cdag->getNode(i)->succsList[j] << " ";
                        tempSet.insert(cdagIdToMacroDagIdMap[cdag->getNode(i)->succsList[j]]);
                        // cout << " mapped to " << cdagIdToMacroDagIdMap[cdag->getNode(i)->succsList[j]];
                        // cout << "\n";
                    }
                }

                for(set<Id>::iterator it = tempSet.begin();
                    it != tempSet.end(); ++it)
                {
                    temp->succsList.push_back(*it);
                    nodePredList[*it].push_back(temp->dynId);
                }
                temp->writeToStreamInBinary(tempMacroGraphFileWrite);
            }
            //cout << "\n" << i << " : " << temp->dynId;
        }
        delete temp;

        tempMacroGraphFileWrite.close();

        string macroGraphFileName = "diskmacrograph";
        ofstream macroGraphFile;
        macroGraphFile.open(macroGraphFileName.c_str(), ios::binary);
        ifstream tempMacroGraphFileRead(tempMacroGraphFileName.c_str());
        GraphNodeWithIV *tempNodeForFinalWrite = new GraphNodeWithIV();
        while(!tempMacroGraphFileRead.eof())
        {
            tempNodeForFinalWrite->readNodeFromBinaryFile(tempMacroGraphFileRead);
            if(tempMacroGraphFileRead.eof())
                break;
            assert(tempNodeForFinalWrite->predsList.size() == 0);
            tempNodeForFinalWrite->predsList.clear();
            set<Id> uniquePredsSet;
            for(Id i=0; i<nodePredList[tempNodeForFinalWrite->dynId].size(); ++i)
            {
                uniquePredsSet.insert(nodePredList[tempNodeForFinalWrite->dynId][i]);
            }
            for(set<Id>::iterator it = uniquePredsSet.begin(); it != uniquePredsSet.end(); ++it)
            {
                tempNodeForFinalWrite->predsList.push_back(*it);
            }
            //tempNodeForFinalWrite->print(cout);
            tempNodeForFinalWrite->writeToStreamInBinary(macroGraphFile);
            tempNodeForFinalWrite->reset();
        }

        macroGraphFile.close();
        

        // DiskCache<GraphNodeWithIV, Id> *macroDagCache = new DiskCache<GraphNodeWithIV, Id>(5120, 48);
        // if(!macroDagCache->init(macroGraphFileName))
        // {
        //     cout <<"\n Cache initialization failed for MACRO dag..stopping execution";
        //     return;
        // }
        // cout << "\n Macro Dag Disk Cache initialized.";

        // Test Code
        // cout <<"\n Printing MACRO DAG";
        // for(Id i = 0; i < macroGraphSize; ++i)
        // {
        //     macroDagCache->getData(i)->print(cout);
        // }
        //delete macroDagCache;

        delete tempNodeForFinalWrite;
        cdagIdToMacroDagIdMap.clear();
        remove(tempMacroGraphFileName.c_str());
    }

    bool isSpecialNode(Id &curNodeId)
    {
        return utils::isBitSet(mergedNodesBitSet, curNodeId, numOfBytesForReadyNodeBitSet);
    }

    Id getOrigCDAGNodeId(Id &macroNodeId)
    {
        return macroDagIdToCdagIdMap[macroNodeId];
    }

    void printMacroNodeToOrigNodeMap()
    {
        cout << "\n Printing macro node to original nodes map : ";
        for(int i=0; i < macroDagIdToCdagIdMap.size(); ++i)
        {
            cout << i << " : " << macroDagIdToCdagIdMap[i] << "\n";
        }
        cout << "\n";
    }


public:
    BYTE *mergedNodesBitSet;
    size_t numOfBytesForReadyNodeBitSet;
    vector<Id> cdagIdToMacroDagIdMap;
    vector<Id> macroDagIdToCdagIdMap;

    deque< deque<Id> > nodePredList;

};
}