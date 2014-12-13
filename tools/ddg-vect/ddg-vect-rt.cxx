/* ddg-vect-rt.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/common/types.hxx"
#include "ddg/analysis/Trace.hxx"
#include "LazyPartitionGraph.hxx"

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Process.h>

#include <memory>
#include <queue>
#include <cstdlib>
#include <iostream>
#include <algorithm>

using namespace ddg;
using namespace llvm;
using namespace std;

class PartitionStats : public DynInstConsumer
{
private:
  struct Stats
  {
    int count;
    ExecutionId begin;
    ExecutionId end;

    AddressTuple firstAddrTuple;
    bool isConstant[3];
  };

  struct Event
  {
    enum {
      Stop, Start
    } type;
    ExecutionId time;
    int count;

    bool operator< (const Event &other) const
    {
      if (time == other.time) {
        return type < other.type;
      }
      return time < other.time;
    }

  };

public:
  void consumeDynInst(Instruction *instr, int contextId,
                      ExecutionId executionId, int timestamp,
                      AddressTuple &addrTuple)
  {
    DenseMap<int, Stats> &partStats = partitions[
      make_pair(cast<ConstantInt>(instr->getMetadata("id")->getOperand(0))->getZExtValue(),
                contextId)];
    if (!partStats.count(timestamp)) {
      Stats &stats = partStats[timestamp];
      stats.count = 0;
      stats.begin = executionId;

      stats.firstAddrTuple = addrTuple;
      stats.isConstant[0] = stats.isConstant[1] = stats.isConstant[2] = true;
    }
    Stats &stats = partStats[timestamp];
    stats.count++;
    stats.end = executionId;

    AddressTuple &firstAddr = stats.firstAddrTuple;
    if (firstAddr.r1 != addrTuple.r1) {
      stats.isConstant[0] = false;
    }
    if (firstAddr.r2 != addrTuple.r2) {
      stats.isConstant[1] = false;
    }
    if (firstAddr.w != addrTuple.w) {
      stats.isConstant[2] = false;
    }
  }

  void dumpMaxMemSize()
  {
    vector<Event> allParts;

    long numConstAddrParts = 0;
    long numConstAddrInsts = 0;

    long totalParts = 0;
    long totalInsts = 0;

    long numSingletonParts = 0;
    
    for (PartitionsMap::iterator p = partitions.begin(); p != partitions.end(); ++p) {
      DenseMap<int, Stats> &statsMap = p->second;
      for (DenseMap<int, Stats>::iterator s = statsMap.begin(); s != statsMap.end();
           ++s) {
        Stats &st = s->second;

        ++totalParts;
        totalInsts += st.count;

        if (st.count == 1) {
          ++numSingletonParts;
        }

        if (st.isConstant[0] == true && st.isConstant[1] == true &&
            st.isConstant[2] == true) {
          ++numConstAddrParts;
          numConstAddrInsts += st.count;
        }
        else if (st.count > 1) {
          Event start = { Event::Start, st.begin, st.count };
          Event stop = { Event::Stop, st.end, st.count };
          allParts.push_back(start);
          allParts.push_back(stop);
        }
      }
      statsMap.clear();
    }
    partitions.clear();
    sort(allParts.begin(), allParts.end());

    int currSize = 0;
    int maxSize = 0;
    for (vector<Event>::iterator it = allParts.begin(); it != allParts.end(); ++it) {
      Event &event = *it;
      if (event.type == Event::Start) {
        currSize += event.count;
        if (currSize > maxSize) {
          maxSize = currSize;
        }
      }
      else {
        currSize -= event.count;
      }
    }

    cout << "Total partitions: " << totalParts << "\n"
         << "Total dynamic instructions: " << totalInsts << "\n"
         << "Max address tuples in memory: " << maxSize << "\n"
         << "Num const addr partitions: " << numConstAddrParts << "\n"
         << "Num const addr instructions: " << numConstAddrInsts << "\n"
         << "Num singleton partitions: " << numSingletonParts << "\n";
  }

private:
  typedef DenseMap<pair<int, int>, DenseMap<int, Stats> > PartitionsMap;
  PartitionsMap partitions;
};

struct Init
{
  Init(Module *mod, Ids &ids)
                  : ids(ids), context(getGlobalContext()),
                    timestamper(ids, storeUses, partitionStats),
                    builder(timestamper, ids), handler(ids, builder)
  {
    module.reset(mod);
  }

  void init()
  {
    storeUses.visit(*module.get());
  }

  void cleanup()
  {
    handler.handleEndTrace();
    partitionStats.dumpMaxMemSize();
  }

  llvm_shutdown_obj shutdownObj;
  LLVMContext &context;

  auto_ptr<Module> module; 
  Ids &ids;

  StoreUses storeUses;
  PartitionStats partitionStats;
  Timestamper timestamper;
  LazyGraphBuilder<Timestamper::Payload> builder;
  TraceEventsHandler<LazyGraphBuilder<Timestamper::Payload> > handler;
};

static Init *init;
static Ids ids;
static TraceEventsHandler<LazyGraphBuilder<Timestamper::Payload> > *handler;

extern "C"
{

void ddg_init()
{
  SMDiagnostic err;

  Module *module = ParseIRFile(getenv("DDG_IR_FILE"), err, getGlobalContext());

  if (module == 0) {
    err.print("ddg-vect-rt", errs());
    exit(1);
  }

  ids.runOnModule(*module);

  init = new Init(module, ids);
  handler = &init->handler;
  init->init();
}

void ddg_cleanup()
{
  init->cleanup();
  delete init;
}

void ddg_start_trace()
{
  init->timestamper.enterTraceRegion();
}

void ddg_start_trace_()
{
  ddg_start_trace();
}

void ddg_stop_trace()
{
  init->timestamper.exitTraceRegion();
}

void ddg_stop_trace_()
{
  ddg_stop_trace();
}

void ddg_load(Id dest, void* src)
{
  Address addr = reinterpret_cast<Address>(src);
  Ld ld = {dest, addr};
  handler->handleLd(&ld);
}

void ddg_store(Id instrId, void* dest)
{
  Address addr = reinterpret_cast<Address>(dest);
  St st = {instrId, addr};
  handler->handleSt(&st);
}

void ddg_function_call(Id instrId, void *address)
{
  Address addr = reinterpret_cast<Address>(address);
  FnCall fnCall = {instrId, addr};
  handler->handleFnCall(&fnCall);
}

void ddg_function_ret(Id instrId)
{
  FnReturn fnRet = {instrId};
  handler->handleFnReturn(&fnRet);
}

void ddg_function_enter(Id fnId, void *address)
{
  Address addr = reinterpret_cast<Address>(address);
  FnEnter fnEnter = {fnId, addr};
  handler->handleFnEnter(&fnEnter);
}

void ddg_basic_block_enter(Id bbId)
{
  BB bb = {bbId};
  handler->handleBB(&bb);
}

void ddg_loop_begin(Id loopId)
{
  LoopBegin loopBegin = {loopId};
  handler->handleLoopBegin(&loopBegin);
}

void ddg_loop_end(Id loopId)
{
  LoopEnd loopEnd = {loopId};
  handler->handleLoopEnd(&loopEnd);
}

void ddg_loop_indvar32(Id instId, int32_t iv)
{
  LoopIndVar loopIndVar = {instId, static_cast<boost::int64_t>(iv)};
  handler->handleLoopIndVar(&loopIndVar);
}

void ddg_loop_indvar64(Id instId, boost::int64_t iv)
{
  LoopIndVar loopIndVar = {instId, iv};
  handler->handleLoopIndVar(&loopIndVar);
}

void ddg_loop_enter(Id loopId)
{
  LoopEnter loopEnter = {loopId};
  handler->handleLoopEnter(&loopEnter);
}

void ddg_loop_exit(Id loopId)
{
  LoopExit loopExit = {loopId};
  handler->handleLoopExit(&loopExit);
}

}

