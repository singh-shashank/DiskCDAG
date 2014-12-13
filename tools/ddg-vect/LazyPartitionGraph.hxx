/* LazyPartitionGraph.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef LAZYPARTITIONGRAPH_HXX
#define LAZYPARTITIONGRAPH_HXX

#include "ddg/analysis/LazyGraph.hxx"
#include "TimestampVector.hxx"

#include <llvm/Pass.h>
#include <llvm/Support/InstVisitor.h>

#include <db_cxx.h>

#include <map>

namespace ddg
{

using namespace llvm;
using namespace std;

struct AddressTuple
{
  Address r1;
  Address r2;
  Address w;

  bool operator < (const AddressTuple &other) const
  {
    if(this->r1 < other.r1)
    {
      return true;
    }
    else if(this->r1 == other.r1)
    {
      if(this->r2 < other.r2)
      {
        return true;
      }
      else if(this->r2 == other.r2)
      {
        if(this->w < other.w)
        {
          return true;
        }
        else
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }
    else
    {
      return false;
    }
  }

  AddressTuple operator - (const AddressTuple &other) const
  {
    AddressTuple result;
    result.r1 = r1 - other.r1;
    result.r2 = r2 - other.r2;
    result.w = w - other.w;
    return result;
  }

  bool operator == (const AddressTuple &other) const
  {
    return r1 == other.r1 && r2 == other.r2 && w == other.w;
  }
};


class DynInstDbIterator;

class DynInstDb
{
public:
  DynInstDb(string dbName);

  ~DynInstDb();

  struct InstKey
  {
    Id id;
    ExecutionId executionId;
  };

  struct InstAttrs
  {
    AddressTuple addressTuple;
    int timestamp;
    int callContext;
  };

  void put(InstKey &key, InstAttrs &attrs);
  bool get(InstKey &key, /*out*/ InstAttrs &attrs);

  typedef DynInstDbIterator iterator;
  iterator begin();
  iterator end();

private:
  Db db;
};


class PartitionDbIterator;

class PartitionDb
{
public:
  PartitionDb(string dbName, bool readOnly = false);

  ~PartitionDb();

  struct Key
  {
    Id id;
    int callContext;
    int partId;
    AddressTuple addressTuple;
  };


  void put(Key &key, int timestamp);
  void getRange(Key &key, int &timestamp);

  void load(DynInstDb &instDb);
  void unitPartitionAndLoad(PartitionDb &indep);
  void nonUnitPartitionAndLoad(PartitionDb &unit);

  typedef PartitionDbIterator iterator;
  iterator begin();
  iterator end();

private:
  Db db;
};

class StoreUses : public InstVisitor<StoreUses, void>
{
public:
  void visitStoreInst(StoreInst &storeInst);

  bool isStoreUse(Instruction *instr)
  {
    return instStores.count(instr);
  }

private:
  DenseMap<Instruction*, StoreInst*> instStores;
};


class DynInstConsumer
{
public:
  virtual void consumeDynInst(Instruction *instr, int contextId,
                              ExecutionId executionId, int timestamp,
                              AddressTuple &addrTuple) = 0;
};


class Partitioner : public DynInstConsumer
{
public:
  Partitioner(PartitionDb &db) : db(db) {}

  void consumeDynInst(Instruction *instr, int contextId,
                      ExecutionId executionId, int timestamp,
                      AddressTuple &addrTuple);

private:
  PartitionDb &db;
};


class Timestamper : public LazyGraphVisitor<TimestampVector>
{
public:
  Timestamper(Ids &ids, StoreUses &storeUses, DynInstConsumer &consumer)
    : ids(ids), storeUses(storeUses), consumer(consumer), timestampVectorLength(0),
      inTraceRegion(false)
  {
    instrIdxs.resize(ids.getNumInsts(), -1);
  }

  ~Timestamper()
  {
    printf("\n TimeStampVectorLength = %d", timestampVectorLength);
   }


  void visitNode(LazyNode<TimestampVector> *node,
                 Predecessors &predecessors,
                 Operands &operands);


  void enterTraceRegion()
  {
    inTraceRegion = true;
  }

  void exitTraceRegion()
  {
    inTraceRegion = false;
  }

  void dumpCallContexts();

private:
  int timestampVectorLength;
  vector<int> instrIdxs;
  DenseMap<Instruction*, AddressTuple> instAddrs;

  bool inTraceRegion;

  Ids &ids;
  StoreUses &storeUses;
  DynInstConsumer &consumer;
};


class LazyPartitionGraph : public ModulePass
{
public:
  static char ID;

  LazyPartitionGraph() : ModulePass(ID) {}

  bool runOnModule(Module &module);
  void getAnalysisUsage(AnalysisUsage &usage) const;
};

}

#endif

