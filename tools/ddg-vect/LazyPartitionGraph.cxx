/* LazyPartitionGraph.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/DebugStr.hxx"
#include "LazyPartitionGraph.hxx"

#include "picojson.h"

#define DEBUG_TYPE "ddg-vect"
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Module.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/FileSystem.h>

#include <boost/iterator/iterator_facade.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include <set>
#include <iostream>

// TODO
//  - Define a typedef for timestamp

namespace ddg
{

using namespace llvm;
using namespace llvm::sys;
using namespace boost;
using namespace std;

static cl::opt<bool> ignoreContext("ignore-context", cl::desc("Ignore context"));
static cl::opt<string> dir("dir", cl::desc("Analysis Dir"), cl::init("."));


class CursorCloser
{
public:
  CursorCloser(Dbc *cursor) : cursor(cursor) { }
  ~CursorCloser() { cursor->close(); }
private:
  Dbc *cursor;
};


class DynInstDbIterator : public iterator_facade<DynInstDbIterator,
                                                 pair<DynInstDb::InstKey, DynInstDb::InstAttrs>,
                                                 single_pass_traversal_tag>
{
public:
  DynInstDbIterator(DynInstDbIterator const &other)
    : cursor(other.cursor), ret(other.ret), closer(other.closer)
  {
  }

private:
  friend class boost::iterator_core_access;
  friend class DynInstDb;

  DynInstDbIterator(Dbc *cursor, bool end)
    : cursor(cursor), closer(new CursorCloser(cursor))
  {
    Dbt dbKey, dbData;
    if (end) {
      cursor->get(&dbKey, &dbData, DB_LAST);
      ret = cursor->get(&dbKey, &dbData, DB_NEXT);
    }
    else {
      ret = cursor->get(&dbKey, &dbData, DB_FIRST);
    }
  }

  bool equal(DynInstDbIterator const &other) const
  {
    if (ret != 0) {
      return ret == other.ret;
    }
    if (other.ret != 0) {
      return false;
    }
    int result;
    cursor->cmp(other.cursor, &result, 0);
    return result == 0 && ret == other.ret;
  }

  void increment()
  {
    Dbt dbKey, dbData;
    ret = cursor->get(&dbKey, &dbData, DB_NEXT);
  }

  pair<DynInstDb::InstKey, DynInstDb::InstAttrs>& dereference() const
  {
    Dbt dbKey, dbData;
    cursor->get(&dbKey, &dbData, DB_CURRENT);

    DynInstDb::InstKey *key = static_cast<DynInstDb::InstKey*>(dbKey.get_data());
    DynInstDb::InstAttrs *attrs = static_cast<DynInstDb::InstAttrs*>(dbData.get_data());

    record.first = *key;
    record.second = *attrs;
    return record;
  }

  Dbc *cursor;
  int ret;
  shared_ptr<CursorCloser> closer;

  mutable pair<DynInstDb::InstKey, DynInstDb::InstAttrs> record;
};


static int dynInstDbCompare(Db *db, const Dbt *a, const Dbt *b)
{
  DynInstDb::InstKey *instA = static_cast<DynInstDb::InstKey*>(a->get_data());
  DynInstDb::InstKey *instB = static_cast<DynInstDb::InstKey*>(b->get_data());
  if (instA->id != instB->id) {
    return instA->id - instB->id;
  }
  else {
    return instA->executionId - instB->executionId;
  }
}

DynInstDb::DynInstDb(string dbName) : db(NULL, 0)
{
  db.set_error_stream(&cerr);
  db.set_bt_compare(dynInstDbCompare);
  db.open(NULL, dbName.c_str(), NULL, DB_BTREE, DB_CREATE | DB_TRUNCATE, 0);
}

DynInstDb::~DynInstDb()
{
  db.close(0);
}

void DynInstDb::put(InstKey &key, InstAttrs &attrs)
{
  Dbt dbKey(&key, sizeof(InstKey));
  Dbt dbData(&attrs, sizeof(InstAttrs));

  db.put(NULL, &dbKey, &dbData, 0);
}

bool DynInstDb::get(InstKey &key, /*out*/ InstAttrs &attrs)
{
  Dbt dbKey(&key, sizeof(InstKey));
  Dbt dbData;

  db.get(NULL, &dbKey, &dbData, 0);
  InstAttrs *attrsP = static_cast<InstAttrs*>(dbData.get_data());

  if (!attrsP) {
    return false;
  }

  attrs = *attrsP;

  return true;
}

DynInstDb::iterator DynInstDb::begin()
{
  Dbc *cursor;
  db.cursor(NULL, &cursor, 0);
  return DynInstDbIterator(cursor, false);
}

DynInstDb::iterator DynInstDb::end()
{
  Dbc *cursor;
  db.cursor(NULL, &cursor, 0);
  return DynInstDbIterator(cursor, true);
}


class UnitStride
{
public:
  static bool isValid(const AddressTuple &tuple)
  {
    return isValidStride(tuple.r1) &&
           isValidStride(tuple.r2) &&
           isValidStride(tuple.w);
  }

private:
  static bool isValidStride(Address stride)
  {
    return stride == 0 || stride == 1 || stride == 2 || stride == 4 || stride == 8;
  }
};


class AnyStride
{
public:
  static bool isValid(const AddressTuple &tuple)
  {
    return true;
  }
};


class PartitionDbIterator : public iterator_facade<PartitionDbIterator,
                                        pair<PartitionDb::Key, int>,
                                        single_pass_traversal_tag>
{
public:
  PartitionDbIterator(PartitionDbIterator const &other) : cursor(other.cursor), ret(other.ret), closer(other.closer)
  {
  }

private:
  friend class boost::iterator_core_access;
  friend class PartitionDb;

  PartitionDbIterator(Dbc *cursor, bool end) : cursor(cursor), closer(new CursorCloser(cursor))
  {
    Dbt dbKey, dbData;
    if (end) {
      ret = cursor->get(&dbKey, &dbData, DB_LAST);
      if (ret == 0) {
        ret = cursor->get(&dbKey, &dbData, DB_NEXT);
      }
    }
    else {
      ret = cursor->get(&dbKey, &dbData, DB_FIRST);
    }
  }

  bool equal(PartitionDbIterator const &other) const
  {
    if (ret != 0) {
      return ret == other.ret;
    }
    if (other.ret != 0) {
      return false;
    }
    int result;
    cursor->cmp(other.cursor, &result, 0);
    return result == 0;
  }

  void increment()
  {
    Dbt dbKey, dbData;
    ret = cursor->get(&dbKey, &dbData, DB_NEXT);
  }

  pair<PartitionDb::Key, int>& dereference() const
  {
    Dbt dbKey, dbData;
    cursor->get(&dbKey, &dbData, DB_CURRENT);

    PartitionDb::Key *key = static_cast<PartitionDb::Key*>(dbKey.get_data());
    int *attrs = static_cast<int*>(dbData.get_data());

    record.first = *key;
    record.second = *attrs;
    return record;
  }

  Dbc *cursor;
  int ret;
  shared_ptr<CursorCloser> closer;

  mutable pair<PartitionDb::Key, int> record;
};

struct Partition
{
  void insert(AddressTuple *addr)
  {
    if (addrs.size() == 1) {
      stride = *(addr) - *(addrs.back());
    }
    addrs.push_back(addr);
  }

  bool matchesStride(AddressTuple* addr)
  {
    if (addrs.size() < 2) return true;
    else return (*addr) - (*addrs.back()) == stride;
  }

  vector<AddressTuple*> addrs;
  AddressTuple stride;
};

static int partitionDbCompare(Db *db, const Dbt *a, const Dbt *b)
{
  PartitionDb::Key *keyA = static_cast<PartitionDb::Key*>(a->get_data());
  PartitionDb::Key *keyB = static_cast<PartitionDb::Key*>(b->get_data());

  int cmp;
  cmp = keyA->id - keyB->id;
  if (cmp != 0) return cmp;

  cmp = keyA->callContext - keyB->callContext;
  if (cmp != 0) return cmp;

  cmp = keyA->partId - keyB->partId;
  if (cmp != 0) return cmp;

  if (keyA->addressTuple < keyB->addressTuple) {
    return -1;
  }
  else if (keyA->addressTuple == keyB->addressTuple) {
    return 0;
  }
  else {
    return 1;
  }
}

PartitionDb::PartitionDb(string dbName, bool readOnly) : db(NULL, 0)
{
  db.set_error_stream(&cerr);
  db.set_bt_compare(partitionDbCompare);
  db.set_flags(DB_DUP);
  int flags = readOnly ? DB_RDONLY : DB_CREATE | DB_TRUNCATE;
  db.open(NULL, dbName.c_str(), NULL, DB_BTREE, flags, 0);
}

PartitionDb::~PartitionDb()
{
  db.close(0);
}

void PartitionDb::put(Key &key, int timestamp)
{
  Dbt dbKey(&key, sizeof(Key));
  Dbt dbData(&timestamp, sizeof(int));

  db.put(NULL, &dbKey, &dbData, 0);
}

void PartitionDb::getRange(Key &key, int &timestamp)
{
  Dbt dbKey(&key, sizeof(Key));
  Dbt dbData;

  Dbc *cursor;
  db.cursor(NULL, &cursor, 0);
  cursor->get(&dbKey, &dbData, DB_SET_RANGE);
  cursor->close();

  key = *static_cast<Key*>(dbKey.get_data());
  timestamp = *static_cast<int*>(dbData.get_data());
}

void PartitionDb::load(DynInstDb &instDb)
{
  for (DynInstDb::iterator it = instDb.begin(), itEnd = instDb.end();
       it != itEnd; ++it) {
    DynInstDb::InstKey &instKey = it->first;
    DynInstDb::InstAttrs &instAttrs = it->second;
    Key key = { instKey.id, instAttrs.callContext,
                instAttrs.timestamp, instAttrs.addressTuple };
    put(key, instAttrs.timestamp);
  }
}

void PartitionDb::unitPartitionAndLoad(PartitionDb &indep)
{
  Id prevId = -1;
  int prevCallContext = -1;
  int prevPartId = -1;
  AddressTuple prevAddressTuple;

  int currPartId = 0;
  AddressTuple currStride;
  int currPartCount = 0;

  for (PartitionDb::iterator it = indep.begin(), itEnd = indep.end();
       it != itEnd; ++it) {
    Id id = it->first.id;
    int callContext = it->first.callContext;
    int partId = it->first.partId;
    AddressTuple addressTuple = it->first.addressTuple;
    int timestamp = it->second;

    bool newPart = false;
    if (prevId != id || prevCallContext != callContext) {
      newPart = true;
    }
    else if (prevPartId != partId) {
      newPart = true;
    }

    if (!newPart) {
      if (currPartCount == 1) {
        currStride = addressTuple - prevAddressTuple;
        newPart = !UnitStride::isValid(currStride);
      }
      else {
        AddressTuple nextStride = addressTuple - prevAddressTuple;
        newPart = !(currStride == nextStride);
      }
    }

    if (newPart) {
      currPartCount = 1;
      ++currPartId;
    }
    else {
      ++currPartCount;
    }

    PartitionDb::Key key = { id, callContext, currPartId, addressTuple };
    put(key, timestamp);

    prevId = id;
    prevCallContext = callContext;
    prevPartId = partId;
    prevAddressTuple = addressTuple;
  }
}

void PartitionDb::nonUnitPartitionAndLoad(PartitionDb &unit)
{
  typedef DenseMap<pair<Id, int>, vector<int> > UnitPartsMap;
  UnitPartsMap unitParts;

  Id prevId = -1;
  int prevCallContext = -1;
  int prevPartId = -1;

  int currPartCount = 0;

  for (PartitionDb::iterator it = unit.begin(), itEnd = unit.end();
       it != itEnd; ++it) {
    Id id = it->first.id;
    int callContext = it->first.callContext;
    int partId = it->first.partId;

    bool newPart = false;
    if (prevId != id || prevCallContext != callContext) {
      newPart = true;
    }
    else if (prevPartId != partId) {
      newPart = true;
    }

    if (newPart) {
      if (currPartCount == 1) {
        unitParts[make_pair(prevId, prevCallContext)].push_back(prevPartId);
      }
      currPartCount = 1;
    }
    else {
      ++currPartCount;
    }

    prevId = id;
    prevCallContext = callContext;
    prevPartId = partId;
  }

  if (currPartCount == 1) {
    unitParts[make_pair(prevId, prevCallContext)].push_back(prevPartId);
  }

  for (UnitPartsMap::iterator it = unitParts.begin(); it != unitParts.end(); ++it) {
    Id id = it->first.first;
    int cc = it->first.second;
    vector<int>& parts = it->second;

    DenseMap<int, vector<AddressTuple> > addrsMap;
    for (vector<int>::iterator pIt = parts.begin(); pIt != parts.end(); ++pIt) {
      int partId = *pIt;
      Key key = { id, cc, partId, {0,0,0} };
      int timestamp;
      unit.getRange(key, timestamp);
      addrsMap[timestamp].push_back(key.addressTuple);
    }

    int partId = 0;
    for (DenseMap<int, vector<AddressTuple> >::iterator aIt = addrsMap.begin();
         aIt != addrsMap.end(); ++aIt) {
      int timestamp = aIt->first;
      vector<AddressTuple>& addrs = aIt->second;
      sort(addrs.begin(), addrs.end());

      vector<Partition> partitions;

      list<AddressTuple*> opsList;
      for (vector<AddressTuple>::iterator vIt = addrs.begin(); vIt != addrs.end();
           ++vIt) {
        opsList.push_back(&*vIt);
      }
      list<AddressTuple*> restartList;

      bool newPart = true;

      for (list<AddressTuple*>::iterator exIt = opsList.begin(); ;)
      {
        if (exIt == opsList.end()) {
          if (!restartList.empty()) {
            swap(restartList, opsList);
            newPart = true;
            exIt = opsList.begin();
            continue;
          }
          else {
            break;
          }
        }

        if(newPart)
        {
          newPart = false;

          partitions.push_back(Partition());
          partitions.back().insert(*exIt);
          exIt = opsList.erase(exIt);
        }
        else
        {
          Partition &part = partitions.back();
          if (part.addrs.size() == 2 && !part.matchesStride(*exIt)) {
            AddressTuple *first  = part.addrs[0];
            AddressTuple *second = part.addrs[1];

            restartList.push_back(first);
            part.addrs.clear();
            part.insert(second);
            part.insert(*exIt);
            exIt = opsList.erase(exIt);
            continue;
          }
          while (exIt != opsList.end() && !part.matchesStride(*exIt)) {
            restartList.push_back(*exIt);
            exIt = opsList.erase(exIt);
          }

          if (exIt != opsList.end()) {
            part.insert(*exIt);
            exIt = opsList.erase(exIt);
          }
        }
      }

      for (vector<Partition>::iterator pIt = partitions.begin();
           pIt != partitions.end(); ++pIt, ++partId) {
        vector<AddressTuple*> &addrs = pIt->addrs;
        for (vector<AddressTuple*>::iterator vIt = addrs.begin();
             vIt != addrs.end(); ++vIt) {
          Key key = { id, cc, partId, **vIt };
          put(key, timestamp);
        }
      }

    }
  }
}


PartitionDb::iterator PartitionDb::begin()
{
  Dbc *cursor;
  db.cursor(NULL, &cursor, DB_CURSOR_BULK);
  return PartitionDbIterator(cursor, false);
}

PartitionDb::iterator PartitionDb::end()
{
  Dbc *cursor;
  db.cursor(NULL, &cursor, 0);
  return PartitionDbIterator(cursor, true);
}


void StoreUses::visitStoreInst(StoreInst &storeInst)
{
  if (Instruction *inst = dyn_cast<Instruction>(storeInst.getValueOperand())) {
    if (inst->isBinaryOp() && inst->getType()->isFloatingPointTy()) {
      instStores[inst] = &storeInst;
    }
  }
}


void Partitioner::consumeDynInst(Instruction *instr, int contextId,
                                 ExecutionId executionId, int timestamp,
                                 AddressTuple &addrTuple)
{
  PartitionDb::Key key = {
    cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
    contextId, timestamp, addrTuple
  };

  db.put(key, timestamp);
}


void Timestamper::visitNode(LazyNode<TimestampVector> *node,
                            Predecessors &predecessors,
                            Operands &operands)
{
  Instruction *instr = node->getInstruction();
  Id instrId = ids.getId(instr);

  int instrIdx = instrIdxs[instrId];
  if (inTraceRegion && instrIdx == -1 &&
      instr->isBinaryOp() && instr->getType()->isFloatingPointTy()) {
    instrIdx = instrIdxs[instrId] = timestampVectorLength;
    ++timestampVectorLength;
  }

  TimestampVector &timestamps = node->get();
  if (instrIdx == -1 && predecessors.size() == 1) {
    timestamps = predecessors[0]->get();
  }
  else {
    timestamps.resize(timestampVectorLength);
    for (Predecessors::iterator pred = predecessors.begin(), predEnd = predecessors.end(); 
         pred != predEnd;
         ++pred) {
      TimestampVector &predTimestamps = (*pred)->get();
      for (int i=0, iMax = min(timestampVectorLength, predTimestamps.size());
           i<iMax; ++i) {
        timestamps[i] = max(timestamps[i], predTimestamps[i]);
      }
    }
  }
  if (instrIdx != -1) {
    timestamps[instrIdx]++;
  }

  if (isa<StoreInst>(instr)) {
    if (predecessors.size() && operands[0] == 0
        && instAddrs.count(predecessors[0]->getInstruction())) {
      LazyNode<TimestampVector> *pred = predecessors[0];
      int timestamp = pred->get()[instrIdxs[ids.getId(pred->getInstruction())]];

      consumer.consumeDynInst(pred->getInstruction(),
        ignoreContext ? 0 : pred->getContextId(),
        pred->getExecutionId(), timestamp, instAddrs[pred->getInstruction()]);
    }
  }

  if (instrIdx != -1) {
    AddressTuple tuple = {0, 0, 0};

    for (int i=0; i<operands.size(); ++i) {
      if (operands[i] == 0) {
        tuple.r1 = predecessors[i]->getAddress();
      }
      else if (operands[i] == 1) {
        tuple.r2 = predecessors[i]->getAddress();
      }
    }

    if (storeUses.isStoreUse(instr)) {
      instAddrs[instr] = tuple;
    }
    else {
      consumer.consumeDynInst(instr,
        ignoreContext ? 0 : node->getContextId(),
        node->getExecutionId(), timestamps[instrIdx], tuple);
    }
  }
}

void Timestamper::dumpCallContexts()
{
  picojson::value::array contexts;
  for (map<LoopNest, int>::iterator it = callContexts.begin();
       it != callContexts.end(); ++it) {
    contexts.push_back(picojson::value(picojson::value::array()));

    picojson::value::array &curr = contexts.back().get<picojson::array>();
    curr.push_back(picojson::value(static_cast<double>(it->second)));
    curr.push_back(picojson::value(picojson::value::array()));

    picojson::value::array &ln = curr.back().get<picojson::array>();
    for (LoopNest::const_iterator cIt = it->first.begin();
         cIt != it->first.end(); ++cIt) {
      ln.push_back(picojson::value(picojson::value::array()));
      picojson::value::array &l = ln.back().get<picojson::array>();

      l.push_back(picojson::value(static_cast<double>(cIt->first)));
      l.push_back(picojson::value(static_cast<double>(cIt->second)));
    }
  }

  string file = dir + string("/callcontexts.json");
  ofstream dumpFile(file.c_str());
  dumpFile << picojson::value(contexts).serialize() << endl;
  dumpFile.close();
}


struct RawMetric
{
  int dynInsts;
  DenseMap<int, int> partitions;
};


struct ProcessedMetric
{
  ProcessedMetric(RawMetric &rawMetric)
  {
    numVectorInsts = 0;
    for (DenseMap<int, int>::iterator pIt = rawMetric.partitions.begin();
         pIt != rawMetric.partitions.end(); ++pIt) {
      if (pIt->second > 1) {
        numVectorInsts += pIt->second;
      }
    }

    dynInsts = rawMetric.dynInsts;
    avgVectorLength = static_cast<double>(dynInsts)/rawMetric.partitions.size();
  }

  ProcessedMetric() : dynInsts(0), avgVectorLength(0.0), numVectorInsts(0) { }

  int dynInsts;
  double avgVectorLength;
  int numVectorInsts;
};


class ProgramNode
{
public:
  enum Type
  {
    LoopNode = 1,
    CallNode,
    StatementNode
  };

  virtual Type type() const = 0;

  virtual ~ProgramNode() {}
};

class NonLeafProgramNode : public ProgramNode
{
public:
  virtual ~NonLeafProgramNode()
  {
  }

  typedef DenseSet<ProgramNode*>::iterator child_iterator;

  child_iterator child_begin()
  {
    return children.begin();
  }

  child_iterator child_end()
  {
    return children.end();
  }

  void addChild(ProgramNode* node)
  {
    children.insert(node);
  }

private:
  DenseSet<ProgramNode*> children;
};

class LoopNode : public NonLeafProgramNode
{
public:
  LoopNode() : header(NULL) { }
  LoopNode(BasicBlock *header) : header(header) { }

  BasicBlock* getHeader()
  {
    return header;
  }

  static inline bool classof(const ProgramNode *node) { return node->type() == ProgramNode::LoopNode; };
  virtual Type type() const { return ProgramNode::LoopNode; }

private:
  BasicBlock *header;
};

class CallNode : public NonLeafProgramNode
{
public:
  CallNode() : call(NULL) { }
  CallNode(Instruction *call) : call(call) { }

  Instruction* getCall()
  {
    return call;
  }

  static inline bool classof(const ProgramNode *node) { return node->type() == ProgramNode::CallNode; };
  virtual Type type() const { return ProgramNode::CallNode; }

private:
  Instruction *call;
};

class StatementNode : public ProgramNode
{
public:
  StatementNode() : instruction(NULL) { }
  StatementNode(Instruction *instruction, RawMetric &indepMetric,
                RawMetric &unitMetric, RawMetric &nonUnitMetric)
    : instruction(instruction), indepMetric(indepMetric), unitMetric(unitMetric),
      nonUnitMetric(nonUnitMetric) { }

  Instruction* getInstruction()
  {
    return instruction;
  }

  ProcessedMetric& getIndepMetric()
  {
    return indepMetric;
  }

  ProcessedMetric& getUnitMetric()
  {
    return unitMetric;
  }

  ProcessedMetric& getNonUnitMetric()
  {
    return nonUnitMetric;
  }

  static inline bool classof(const ProgramNode *node) { return node->type() == ProgramNode::StatementNode; };
  virtual Type type() const { return ProgramNode::StatementNode; }

private:
  Instruction *instruction;
  ProcessedMetric indepMetric;
  ProcessedMetric unitMetric;
  ProcessedMetric nonUnitMetric;
};


class ProgramTree
{
public:
  ProgramTree(Ids &ids) : ids(ids) { }

  void build(DenseMap<int, LoopNest>& contexts,
             DenseMap<pair<Id, int>, RawMetric>& indepMetrics,
             DenseMap<pair<Id, int>, RawMetric>& unitMetrics,
             DenseMap<pair<Id, int>, RawMetric>& nonUnitMetrics)
  {
    DenseMap<int, NonLeafProgramNode*> parentNodes;

    for (DenseMap<int, LoopNest>::iterator cIt = contexts.begin(),
                                           cEnd = contexts.end();
         cIt != cEnd; ++cIt) {
      LoopNest &loopNest = cIt->second;
      LoopNest::iterator l = loopNest.begin(),
                         lEnd = loopNest.end();
      while (l != lEnd && l->first == IVEntry::CallSite) {
        ++l;
      }
      if (l == lEnd) {
        continue;
      }

      Id topLoop = l->second;
      bool newLoop = !loops.count(topLoop);
      LoopNode* topNode = getLoop(topLoop);
      if (newLoop) {
        roots.insert(topNode);
      }
      ++l;

      NonLeafProgramNode* currPar = topNode;
      for (; l != lEnd; ++l) {
        NonLeafProgramNode* node;
        if (l->first == IVEntry::CallSite) {
          node = getCall(l->second);
        }
        else {
          node = getLoop(l->second);
          roots.erase(node);
        }
        currPar->addChild(node);
        currPar = node;
      }

      parentNodes[cIt->first] = currPar;
    }

    for (DenseMap<pair<Id, int>, RawMetric>::iterator mIt = indepMetrics.begin();
        mIt != indepMetrics.end(); ++mIt) {
      StatementNode* stmt = createStatement(mIt->first, mIt->second,
                                            unitMetrics[mIt->first],
                                            nonUnitMetrics[mIt->first]);
      Id contextId = mIt->first.second;
      if (parentNodes.count(contextId)) {
        parentNodes[contextId]->addChild(stmt);
      }
    }
  }

  DenseSet<ProgramNode*>& getRoots()
  {
    return roots;
  }

private:
  LoopNode* getLoop(Id loopId)
  {
    if (loops.count(loopId)) {
      return &loops[loopId];
    }
    LoopNode *loopNode = new LoopNode(ids.getLoop(loopId));
    loops.insert(loopId, loopNode);
    return loopNode;
  }

  CallNode* getCall(Id callId)
  {
    if (calls.count(callId)) {
      return &calls[callId];
    }
    CallNode *callNode = new CallNode(ids.getInstruction(callId));
    calls.insert(callId, callNode);
    return callNode;
  }

  StatementNode* createStatement(pair<Id, int> &id, RawMetric &indepMetric,
                                 RawMetric &unitMetric, RawMetric &nonUnitMetric)
  {
    StatementNode* stmtNode = new StatementNode(ids.getInstruction(id.first),
                                                indepMetric, unitMetric,
                                                nonUnitMetric);
    statements.insert(id, stmtNode);
    return stmtNode;
  }

private:
  Ids &ids;

  DenseSet<ProgramNode*> roots;

  ptr_map<Id, LoopNode> loops;
  ptr_map<Id, CallNode> calls;
  ptr_map<pair<Id, int>, StatementNode> statements;
};


class HPCToolkitExporter
{
public:
  HPCToolkitExporter(Module &module, ProgramTree &progTree)
    : progTree(progTree), needPF(true)
  {
    DenseSet<MDNode*> subprograms;
    DenseSet<MDNode*> compileUnits;

    if (NamedMDNode* sps = module.getNamedMetadata("llvm.dbg.sp")) {
      for (int i=0; i<sps->getNumOperands(); ++i) {
        MDNode* spNode = sps->getOperand(i);
        subprograms.insert(spNode);

        DISubprogram sp(spNode);
        MDNode* cu = sp.getCompileUnit();
        compileUnits.insert(cu);
      }
    }

    if (NamedMDNode* cus = module.getNamedMetadata("llvm.dbg.cu")) {
      for (int i=0; i<cus->getNumOperands(); ++i) {
        MDNode* cuNode = cus->getOperand(i);
        compileUnits.insert(cuNode);

        DICompileUnit cu(cuNode);
        DIArray sps = cu.getSubprograms();
        for (int j=0; j<sps.getNumElements(); ++j) {
          subprograms.insert(sps.getElement(j));
        }
      }
    }

    nextId = 1;

    for (DenseSet<MDNode*>::iterator it = compileUnits.begin(); it != compileUnits.end(); ++it) {
      DICompileUnit cu(*it);
      fileTable[cu.getFilename()] = nextId;
      fileDbgs[cu.getFilename()] = cu;
      ++nextId;
    }

    for (DenseSet<MDNode*>::iterator it = subprograms.begin(); it != subprograms.end(); ++it) {
      DISubprogram sp(*it);
      Function *fn;
      if (!(fn = sp.getFunction())) {
        fn = module.getFunction(sp.getName());
      }
      fnDbgs.insert(make_pair(fn, sp));
      procTable.insert(make_pair(fn, nextId));
      ++nextId;
    }
  }

  void dump()
  {
    string s;
    raw_string_ostream ss(s);

    needPF = true;
    DenseSet<ProgramNode*>& roots = progTree.getRoots();
    for (DenseSet<ProgramNode*>::iterator rIt = roots.begin();
         rIt != roots.end(); ++rIt) {
      output(ss, *rIt);
    }

    bool existed;
    fs::create_directories("./database/src", existed);
  
    for (set<string>::iterator fIt = usedFiles.begin(); fIt != usedFiles.end(); ++fIt) {
      DIScope &cu = fileDbgs[*fIt];
      fs::copy_file(Twine(cu.getDirectory()) + "/" + Twine(cu.getFilename()),
                    "./database/src/" + Twine(cu.getFilename()));
    }
    
    string err;
    raw_fd_ostream out("./database/experiment.xml", err);

    out << 
        "<?xml version=\"1.0\"?>\n"
        "<HPCToolkitExperiment version=\"2.0\">\n"
        "<SecCallPathProfile i=\"0\" n=\"prof\">\n"
        "<SecHeader>\n"
        "  <MetricTable>\n"
        "    <Metric i=\"0\" n=\"DYNINSTS\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.0f\" />\n"
        "    <Metric i=\"1\" n=\"INDEP_AVG_VECTOR_SIZE\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.2f\" />\n"
        "    <Metric i=\"2\" n=\"INDEP_VECTOR_INSTS\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.0f\" />\n"
        "    <Metric i=\"3\" n=\"UNIT_AVG_VECTOR_SIZE\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.2f\" />\n"
        "    <Metric i=\"4\" n=\"UNIT_VECTOR_INSTS\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.0f\" />\n"
        "    <Metric i=\"5\" n=\"NONUNIT_DYNINSTS\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.0f\" />\n"
        "    <Metric i=\"6\" n=\"NON_UNIT_AVG_VECTOR_SIZE\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.2f\" />\n"
        "    <Metric i=\"7\" n=\"NON_UNIT_VECTOR_INSTS\" v=\"raw\" t=\"nil\" show=\"1\" show-percent=\"0\" fmt=\"%.0f\" />\n"
        "  </MetricTable>\n";

    out << "  <FileTable>\n";
    for (set<string>::iterator fIt = usedFiles.begin(); fIt != usedFiles.end(); ++fIt) {
      out << "    <File i=\"" << fileTable[*fIt] << "\" "
                       << "n=\"./src/" << *fIt << "\" />\n";
    }
    out << "  </FileTable>\n";
    
    out << "  <ProcedureTable>\n";
    for (DenseSet<Function*>::iterator pIt = usedFns.begin(); pIt != usedFns.end(); ++pIt) {
      out << "    <Procedure i=\"" << procTable[*pIt] << "\" "
                       << "n=\"" << fnDbgs[*pIt].getName() << "\" />\n";
    }
    out << "  </ProcedureTable>\n";

    out << 
      "</SecHeader>\n"
      "<SecCallPathProfileData>\n";

    out << ss.str();

    out <<
      "</PF>\n"
      "</SecCallPathProfileData>\n"
      "</SecCallPathProfile>\n"
      "</HPCToolkitExperiment>\n";
  }

  void output(raw_ostream& out, ProgramNode* node)
  {
    if (LoopNode *loop = dyn_cast<LoopNode>(node)) {
      Instruction *f = loop->getHeader()->getFirstInsertionPt();
      DILocation loc(f->getMetadata("dbg"));

      appendPF(out, f, loc);

      out << "<L i=\"" << nextId << "\" s=\"" << nextId << "\" l=\"" 
          << loc.getLineNumber() << "\" >\n";
      ++nextId;
      outputChildren(out, loop);
      out << "</L>\n";
    }
    else if (CallNode *call = dyn_cast<CallNode>(node)) {
      Instruction *f = call->getCall();
      DILocation loc(f->getMetadata("dbg"));

      appendPF(out, f, loc);

      out << "<C i=\"" << nextId << "\" s=\"" << nextId << "\" l=\"" 
          << loc.getLineNumber() << "\" >\n";
      ++nextId;
      needPF = true;
      outputChildren(out, call);
      out << "</PF>\n";
      out << "</C>\n";
    }
    else if (StatementNode *stmtNode = dyn_cast<StatementNode>(node)){
      Instruction *f = stmtNode->getInstruction();
      DILocation loc(f->getMetadata("dbg"));

      appendPF(out, f, loc);
      out << "<S i=\"" << nextId << "\" s=\"" << nextId << "\" "
                     << "l=\"" << loc.getLineNumber() << "\">\n";
      out << "<M n=\"0\" v=\"" << stmtNode->getIndepMetric().dynInsts << "\" />\n";
      out << "<M n=\"1\" v=\"" << stmtNode->getIndepMetric().avgVectorLength << "\" />\n";
      out << "<M n=\"2\" v=\"" << stmtNode->getIndepMetric().numVectorInsts << "\" />\n";
      out << "<M n=\"3\" v=\"" << stmtNode->getUnitMetric().avgVectorLength << "\" />\n";
      out << "<M n=\"4\" v=\"" << stmtNode->getUnitMetric().numVectorInsts << "\" />\n";
      out << "<M n=\"5\" v=\"" << stmtNode->getNonUnitMetric().dynInsts << "\" />\n";
      out << "<M n=\"6\" v=\"" << stmtNode->getNonUnitMetric().avgVectorLength << "\" />\n";
      out << "<M n=\"7\" v=\"" << stmtNode->getNonUnitMetric().numVectorInsts << "\" />\n";
      out << "</S>\n";
      ++nextId;
    }
  }

  void outputChildren(raw_ostream& out, NonLeafProgramNode *node)
  {
    for (NonLeafProgramNode::child_iterator it = node->child_begin();
         it != node->child_end(); ++it) {
      output(out, *it);
    }
  }

  void appendPF(raw_ostream& out, Instruction *inst, DILocation &loc)
  {
    if (needPF) {
      usedFiles.insert(loc.getFilename());
      usedFns.insert(inst->getParent()->getParent());

      DISubprogram &sp = fnDbgs[inst->getParent()->getParent()];
      out << "<PF i=\"" << nextId << "\" s=\"" << nextId << "\" "
              << "f=\"" << fileTable[loc.getFilename()] << "\" "
              << "l=\"" << sp.getLineNumber() << "\" "
              << "n=\"" << procTable[inst->getParent()->getParent()] << "\" >\n";

      ++nextId;
      needPF = false;
    }
  }

private:
  StringMap<int> fileTable;
  StringMap<DICompileUnit> fileDbgs;

  DenseMap<Function*, int> procTable;
  DenseMap<Function*, DISubprogram> fnDbgs;

  set<string> usedFiles;
  DenseSet<Function*> usedFns;

  ProgramTree &progTree;

  stringstream profileData;
  int nextId;

  bool needPF;
};


void collectMetrics(DenseMap<pair<Id, int>, RawMetric> &indepMetrics,
                    DenseMap<pair<Id, int>, RawMetric> &unitMetrics,
                    DenseMap<pair<Id, int>, RawMetric> &nonUnitMetrics)
{
  PartitionDb indep(dir + string("/indep.db"), true);

  for (PartitionDb::iterator it = indep.begin(), itEnd = indep.end();
       it != itEnd; ++it) {
    RawMetric& metric = indepMetrics[make_pair(it->first.id, it->first.callContext)];
    ++metric.dynInsts;
    ++metric.partitions[it->first.partId];
  }

  PartitionDb unit(dir + string("/unit.db"), true);

  for (PartitionDb::iterator it = unit.begin(), itEnd = unit.end();
       it != itEnd; ++it) {
    RawMetric& metric = unitMetrics[make_pair(it->first.id, it->first.callContext)];
    ++metric.dynInsts;
    ++metric.partitions[it->first.partId];
  }

  PartitionDb nonUnit(dir + string("/nonunit.db"), true);

  for (PartitionDb::iterator it = nonUnit.begin(), itEnd = nonUnit.end();
       it != itEnd; ++it) {
    RawMetric& metric = nonUnitMetrics[make_pair(it->first.id, it->first.callContext)];
    ++metric.dynInsts;
    ++metric.partitions[it->first.partId];
  }
}

void fillContexts(DenseMap<int, LoopNest>& contexts)
{
  picojson::value ccjson;
  string file = dir + string("/callcontexts.json");
  ifstream contextsFile(file.c_str());
  contextsFile >> ccjson;

  picojson::value::array &ccArr = ccjson.get<picojson::array>();
  for (picojson::value::array::iterator it = ccArr.begin();
       it != ccArr.end(); ++it) {
    picojson::value::array &cc = it->get<picojson::array>();
    int id = static_cast<int>(cc[0].get<double>());
    picojson::value::array &calls = cc[1].get<picojson::array>();
    LoopNest &loopNest = contexts[id];
    loopNest.reserve(calls.size());
    for (picojson::value::array::iterator cIt = calls.begin();
         cIt != calls.end(); ++cIt) {
      picojson::value::array &loop = cIt->get<picojson::array>();
      loopNest.push_back(make_pair(static_cast<IVEntry::Type>(loop[0].get<double>()),
                                   static_cast<Id>(loop[1].get<double>())));
    }
  }
}


static cl::opt<bool> metrics("metrics", cl::desc("Calculate metrics"));
static cl::opt<bool> hpctoolkit("hpctoolkit", cl::desc("Export metrics as hpctoolkit xml"));
static cl::opt<bool> force("force", cl::desc("Force creation of dbs even if already present"));

char LazyPartitionGraph::ID = 0;

bool LazyPartitionGraph::runOnModule(Module &module)
{

  if (dir != ".") {
    bool existed;
    fs::create_directory(dir, existed);
  }

  if (hpctoolkit || metrics) {
    Ids& ids = getAnalysis<Ids>();

    DenseMap<pair<Id, int>, RawMetric> indepMetrics;
    DenseMap<pair<Id, int>, RawMetric> unitMetrics;
    DenseMap<pair<Id, int>, RawMetric> nonUnitMetrics;
    collectMetrics(indepMetrics, unitMetrics, nonUnitMetrics);

    DenseMap<int, LoopNest> contexts;
    fillContexts(contexts);

    if (hpctoolkit) {
      ProgramTree tree(ids);
      tree.build(contexts, indepMetrics, unitMetrics, nonUnitMetrics);

      HPCToolkitExporter exp(module, tree);
      exp.dump();
    }

    if (metrics) {
      string err;
      string file = dir + string("/metrics.csv");
      raw_fd_ostream out(file.c_str(), err);

      for (DenseMap<pair<Id, int>, RawMetric>::iterator it = unitMetrics.begin();
           it != unitMetrics.end(); ++it) {
        Id instId = it->first.first;
        int callContext = it->first.second;
        Instruction *instr = ids.getInstruction(instId);

        out << instId << ", ";
        out << DebugStr(instr);
        if (callContext) {
          out << " @ [";
          bool first = true;
          for (LoopNest::iterator cIt = contexts[callContext].begin(),
                                  cEnd = contexts[callContext].end();
               cIt != cEnd; ++cIt) {
            if (cIt->first == IVEntry::Loop) {
              continue;
            }

            if (!first) {
              out << " -> ";
            }
            else {
              first = false;
            }
            out << DebugStr(ids.getInstruction(cIt->second));
          }
          out << "]";
        }
        out << ", ";

        switch(instr->getOpcode()) {
          case Instruction::FAdd:
            out << "FPADD";
            break;
          case Instruction::FSub:
            out << "FPSUB";
            break;
          case Instruction::FMul:
            out << "FPMUL";
            break;
          case Instruction::FDiv:
            out << "FPDIV";
            break;
        }

        out << ", ";

        ProcessedMetric indepM(indepMetrics[it->first]);
        ProcessedMetric unitM(it->second);
        ProcessedMetric nonUnitM(nonUnitMetrics[it->first]);

        out << indepM.dynInsts << ", "
            << llvm::format("%.2f", indepM.avgVectorLength) << ", "
            << indepM.numVectorInsts << ", "
            << llvm::format("%.2f", static_cast<double>(indepM.numVectorInsts * 100)/indepM.dynInsts) << ", ";

        out << unitM.dynInsts << ", "
            << llvm::format("%.2f", unitM.avgVectorLength) << ", "
            << unitM.numVectorInsts << ", "
            << llvm::format("%.2f", static_cast<double>(unitM.numVectorInsts*100)/unitM.dynInsts) << ", ";

        out << nonUnitM.dynInsts << ", "
            << llvm::format("%.2f", nonUnitM.avgVectorLength) << ", "
            << nonUnitM.numVectorInsts << ", "
            << llvm::format("%.2f", static_cast<double>(nonUnitM.numVectorInsts*100)/nonUnitM.dynInsts) << "\n";
      }
    }

    return false;
  }

  string indepDbFile = dir + string("/indep.db");
  string unitDbFile = dir + string("/unit.db");
  string nonUnitDbFile = dir + string("/nonunit.db");

  if (!force && (ifstream(indepDbFile.c_str()) || ifstream(unitDbFile.c_str())
                 || ifstream(nonUnitDbFile.c_str()))) {
    errs() << "Partition dbs exist. Clear them and run again "
           << "or run again with --force option\n";
    return false;
  }

  Ids& ids = getAnalysis<Ids>();

  StoreUses storeUses;
  storeUses.visit(module);

  PartitionDb indep(indepDbFile);
  Partitioner partitioner(indep);

  //DEBUG(dbgs() << "Starting timestamper...\n");

  Timestamper timestamper(ids, storeUses, partitioner);
  timestamper.enterTraceRegion();
  timestamper.visit(ids);

  timestamper.dumpCallContexts();

 

  //DEBUG(dbgs() << "Creating unit partitions...\n");
  PartitionDb unit(unitDbFile);
  unit.unitPartitionAndLoad(indep);

  //DEBUG(dbgs() << "Creating non unit partitions... \n");
  PartitionDb nonunit(nonUnitDbFile);
  nonunit.nonUnitPartitionAndLoad(unit);

  return false;
}

void LazyPartitionGraph::getAnalysisUsage(AnalysisUsage &usage) const
{
  usage.setPreservesAll();
  usage.addRequiredTransitive<Ids>();
}

static RegisterPass<LazyPartitionGraph> lazyPartitionGraph("lazy-partition-graph", "Lazy Partition Graph", false, true);

}

