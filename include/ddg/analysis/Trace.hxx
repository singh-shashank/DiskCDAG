/* Trace.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef TRACE_HXX
#define TRACE_HXX

#include "ddg/analysis/Ids.hxx"

#include <llvm/Instructions.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/CommandLine.h>

#include <sstream>
#include <stack>
#include <vector>
#include <iostream>
#include <cstdio>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

namespace ddg
{

using namespace std;
using namespace llvm;
using namespace boost::iostreams;

// Trace Class Hierarchy

class Trace
{
public:
  enum Type
  {
    FnCallType = 1,
    FnReturnType,
    FnEnterType,
    BBType,
    LdType,
    StType,
    LoopBeginType,
    LoopEndType,
    LoopIndVar32Type,
    LoopIndVar64Type,
    LoopEnterType,
    LoopExitType,
    TraceEnd
  };
};

class FnCall
{
public:
  Id getInstructionId()
  {
    return instructionId;
  }

  Address getAddress()
  {
    return address;
  }

  Id instructionId;
  Address address;
};

class FnReturn
{
public:
  Id getInstructionId()
  {
    return instructionId;
  }

  Id instructionId;
};

class FnEnter
{
public:
  Id getFnId()
  {
    return fnId;
  }

  Address getAddress()
  {
    return address;
  }

  Id fnId;
  Address address;
};

class BB
{
public:
  Id getBbId()
  {
    return bbId;
  }

  Id bbId;
};

class Ld
{
public:
  Id getInstrId()
  {
    return instrId;
  }

  Address getAddress()
  {
    return address;
  }

  Id instrId;
  Address address;
};

class St
{
public:
  Id getInstrId()
  {
    return instrId;
  }

  Address getAddress()
  {
    return address;
  }

  Id instrId;
  Address address;
};

class LoopBegin
{
public:
  Id getLoopId() {
    return loopId;
  }

  Id loopId;
};

class LoopEnd
{
public:
  Id getLoopId() {
    return loopId;
  }

  Id loopId;
};

class LoopIndVar
{
public:
  uint64_t getIv()
  {
    return iv;
  }

  Id instrId;
  uint64_t iv;
};

class LoopEnter
{
public:
  Id getLoopId() {
    return loopId;
  }

  Id loopId;
};

class LoopExit
{
public:
  Id getLoopId() {
    return loopId;
  }

  Id loopId;
};

// Trace Files

class TraceFiles
{
public:
  typedef vector<string>::iterator iterator;

  iterator begin()
  {
    return files.begin();
  }

  iterator end()
  {
    return files.end();
  }

  TraceFiles()
  {
    for (int i=1; ; ++i) {
      stringstream fname;
      fname << i << ".trace";
      ifstream file(fname.str().c_str());
      if (file) {
        files.push_back(fname.str());
      }
      else {
        break;
      }
    }
  }

private:
  vector<string> files;
};


template <typename Derived>
class TraceVisitor
{
public:

  void newTraceFile(string &fileName)
  {
  }

  void visitPHIs(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId, BasicBlock *prevBlock)
  {
  }

  void visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId)
  {
  }

  void visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId)
  {
  }

  void visitStore(StoreInst *storeInst, Address addr, ExecutionId executionId)
  {
  }

  void visitCall(Instruction *call, ExecutionId executionId)
  {
  }

  void visitReturn(Instruction *call, ExecutionId callExecutionId, ReturnInst *ret, ExecutionId retExecutionId)
  {
  }

  void loopBegin(LoopBegin *loopBegin)
  {
  }

  void loopEnd(LoopEnd *loopEnd)
  {
  }

  void loopIndVar(LoopIndVar *loopIndVar)
  {
  }

  void loopEnter(LoopEnter *loopEnter)
  {
  }

  void loopExit(LoopExit *loopExit)
  {
  }
};


#define SUBCLASS(SubClass, obj) static_cast<SubClass*>(&obj)


template <typename Visitor>
class TraceEventsHandler
{
public:
  TraceEventsHandler(Ids &ids, TraceVisitor<Visitor> &visitor)
                        : ids(ids),
                          visitor(visitor),
                          nextExecId(0),
                          mustVisitPrevBB(false),
                          prevBB(NULL),
                          prevInstr(NULL),
                          state(DEFAULT)
  {}

  void handleBB(BB *bb);

  void handleLd(Ld *ld);
  void handleSt(St *st);

  void handleFnCall(FnCall *fnCall);
  void handleFnReturn(FnReturn *fnReturn);
  void handleFnEnter(FnEnter *fnEnter);

  void handleLoopBegin(LoopBegin* loopBegin);
  void handleLoopEnd(LoopEnd *loopEnd);
  void handleLoopEnter(LoopEnter *loopEnter);
  void handleLoopExit(LoopExit *loopExit);
  void handleLoopIndVar(LoopIndVar *loopIndVar);

  void handleEndTrace();

private:
  enum State {
    DEFAULT, AFTER_CALL, SKIP_TILL_MATCHING_RETURN
  };
  State state;
  Address prevCallAddr;
  Id prevCallId;

  TraceVisitor<Visitor> &visitor;
  Ids &ids;

  ExecutionId nextExecId;
  ExecutionId currExecId;
  ExecutionId prevExecId;

  bool mustVisitPrevBB;
  BasicBlock *prevBB;
  Instruction* prevInstr;
  stack<pair<Instruction*, ExecutionId> > callStack;
};

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleBB(BB *bb)
{
  if (state != DEFAULT) {
    return;
  }

  int bbId = bb->getBbId();
  BasicBlock *basicBlock = ids.getBasicBlock(bbId);
  currExecId = nextExecId;
  ++nextExecId;

  if (mustVisitPrevBB) {
    SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);
  }

  if (prevBB && ids.isFirstPHI(bbId)) {
    SUBCLASS(Visitor, visitor)->visitPHIs(basicBlock->begin(), basicBlock->getFirstNonPHI(), currExecId, prevBB);
  }

  prevBB = basicBlock;
  prevExecId = currExecId;
  mustVisitPrevBB = true;
  prevInstr = ids.getFirstInsertionPoint(bbId);

  SUBCLASS(Visitor, visitor)->visitBasicBlockRange(ids.getFirstNonPHI(bbId), prevInstr, currExecId);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLd(Ld *ld)
{
  if (state != DEFAULT) {
    return;
  }

  LoadInst *load = cast<LoadInst>(ids.getInstruction(ld->getInstrId()));

  SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, load, currExecId);
  BasicBlock::iterator nxt = load;
  nxt++;
  prevInstr = nxt;

  SUBCLASS(Visitor, visitor)->visitLoad(load, ld->getAddress(), currExecId);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleSt(St *st)
{
  if (state != DEFAULT) {
    return;
  }

  StoreInst* store = cast<StoreInst>(ids.getInstruction(st->getInstrId()));

  SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, store, currExecId);
  BasicBlock::iterator nxt = store;
  nxt++;
  prevInstr = nxt;
  SUBCLASS(Visitor, visitor)->visitStore(store, st->getAddress(), currExecId);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleFnCall(FnCall *fnCall)
{
  if (state != DEFAULT) {
    return;
  }

  prevCallAddr = fnCall->getAddress();
  prevCallId = fnCall->getInstructionId();

  state = AFTER_CALL;
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleFnEnter(FnEnter *fnEnter)
{
  if (state == AFTER_CALL) {
    if (fnEnter->getAddress() != prevCallAddr) {
      state = SKIP_TILL_MATCHING_RETURN;
    }
    else {
      state = DEFAULT;

      Instruction *call = ids.getInstruction(prevCallId);
      SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, call, currExecId);
      mustVisitPrevBB = false;

      SUBCLASS(Visitor, visitor)->visitCall(call, currExecId);

      callStack.push(make_pair(call, currExecId));
    }
  }
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleFnReturn(FnReturn *fnReturn)
{
  if (state == AFTER_CALL) {
    state = DEFAULT;
    return;
  }
  if (state == SKIP_TILL_MATCHING_RETURN) {
    if (fnReturn->getInstructionId() == prevCallId) {
      state = DEFAULT;
    }
    return;
  }

  Instruction* call = ids.getInstruction(fnReturn->getInstructionId());

  SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);

  while (callStack.top().first != call) {
    SUBCLASS(Visitor, visitor)->visitReturn(callStack.top().first, callStack.top().second, NULL, -1);
    callStack.pop();
  }

  if (ReturnInst *returnInst = dyn_cast<ReturnInst>(prevBB->getTerminator())) {
    SUBCLASS(Visitor, visitor)->visitReturn(call, callStack.top().second, returnInst, currExecId);
  }
  else {
    SUBCLASS(Visitor, visitor)->visitReturn(call, callStack.top().second, NULL, -1);
  }
  callStack.pop();

  currExecId = prevExecId = nextExecId;
  ++nextExecId;

  BasicBlock::iterator nxt = call;
  nxt++;
  prevInstr = nxt;
  prevBB = call->getParent();
  mustVisitPrevBB = true;
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLoopBegin(LoopBegin *loopBegin)
{
  if (state != DEFAULT) {
    return;
  }

  assert(mustVisitPrevBB && "mustVisitPrevBB == true");
  SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);
  mustVisitPrevBB = false;
  SUBCLASS(Visitor, visitor)->loopBegin(loopBegin);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLoopEnd(LoopEnd *loopEnd)
{
  if (state != DEFAULT) {
    return;
  }

  if (mustVisitPrevBB) {
    SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);
  }
  mustVisitPrevBB = false;
  SUBCLASS(Visitor, visitor)->loopEnd(loopEnd);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLoopEnter(LoopEnter *loopEnter)
{
  if (state != DEFAULT) {
    return;
  }

  if (mustVisitPrevBB) {
    SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);
  }
  mustVisitPrevBB = false;
  SUBCLASS(Visitor, visitor)->loopEnter(loopEnter);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLoopExit(LoopExit *loopExit)
{
  if (state != DEFAULT) {
    return;
  }

  SUBCLASS(Visitor, visitor)->loopExit(loopExit);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleLoopIndVar(LoopIndVar *loopIndVar)
{
  if (state != DEFAULT) {
    return;
  }

  SUBCLASS(Visitor, visitor)->loopIndVar(loopIndVar);
}

template <typename Visitor>
void TraceEventsHandler<Visitor>::handleEndTrace()
{
  if (mustVisitPrevBB) {
    SUBCLASS(Visitor, visitor)->visitBasicBlockRange(prevInstr, prevBB->end(), prevExecId);
  }
}


class TraceParser
{
public:
  TraceParser(const string &traceFile) : in(gzip_decompressor() | file_source(traceFile)), tracecnt(0)
  { }

  ~TraceParser()
  {
  }

  template <typename Visitor>
  void parse(TraceEventsHandler<Visitor> &handler);

private:
  template<typename T> inline T next()
  {
    T nxt = 0;
    in.read(reinterpret_cast<char*>(&nxt), sizeof(T));
    return nxt;
  }

  filtering_istream in;
  size_t tracecnt;
};

template <typename Visitor>
void TraceParser::parse(TraceEventsHandler<Visitor> &handler)
{
  //cout << "\n Starting to parse";
  while (in.good()) {
    tracecnt++;
    if(tracecnt%1000 == 0)
    {
      //std::cout << "traceno: " << tracecnt << '\n';
      //fflush(stdout);
    }
    char traceType = -1;
    in.get(traceType);
    //cout<<traceType << " ";

    if (traceType == -1) {
      break;
    }

    switch (traceType) {
      case Trace::FnCallType: {
        Id instrId = next<Id>();
        Address address = next<Address>();

        FnCall fnCall = {instrId, address};
        handler.handleFnCall(&fnCall);
        break;
      }
      case Trace::FnReturnType: {
        Id instrId = next<Id>();
        FnReturn fnReturn = {instrId};
        handler.handleFnReturn(&fnReturn);
        break;
      }
      case Trace::FnEnterType: {
        Id fnId = next<Id>();
        Address address = next<Address>();
        FnEnter fnEnter = {fnId, address};
        handler.handleFnEnter(&fnEnter);
        break;
      }
      case Trace::BBType: {
        Id bbId = next<Id>();
        BB bb = {bbId};
        handler.handleBB(&bb);
        break;
      }
      case Trace::LdType: {
        Id instrId = next<Id>();
        Address addr = next<Address>();
        Ld ld = {instrId, addr};
        handler.handleLd(&ld);
        break;
      }
      case Trace::StType: {
        Id instrId = next<Id>();
        Address addr = next<Address>();
        St st  = {instrId, addr};
        handler.handleSt(&st);
        break;
      }
      case Trace::LoopBeginType: {
        Id loopId = next<Id>();
        LoopBegin loopBegin = {loopId};
        handler.handleLoopBegin(&loopBegin);
        break;
      }
      case Trace::LoopEndType: {
        Id loopId = next<Id>();
        LoopEnd loopEnd = {loopId};
        handler.handleLoopEnd(&loopEnd);
        break;
      }
      case Trace::LoopIndVar32Type: {
        Id instrId = next<Id>();
        int32_t iv = next<int32_t>();
        LoopIndVar loopIndVar = {instrId, static_cast<int64_t>(iv)};
        handler.handleLoopIndVar(&loopIndVar);
        break;
      }
      case Trace::LoopIndVar64Type: {
        Id instrId = next<Id>();
        int64_t iv = next<int64_t>();
        LoopIndVar loopIndVar = {instrId, iv};
        handler.handleLoopIndVar(&loopIndVar);
        break;
      }
      case Trace::LoopEnterType: {
        Id loopId = next<Id>();
        LoopEnter loopEnter = {loopId};
        handler.handleLoopEnter(&loopEnter);
        break;
      }
      case Trace::LoopExitType: {
        Id loopId = next<Id>();
        LoopExit loopExit = {loopId};
        handler.handleLoopExit(&loopExit);
        break;
      }
      default: {
        assert(0 && "Unexpected input");
      }
    }
  }
  handler.handleEndTrace();
}

extern cl::opt<string> traceFile;

class TraceTraversal
{
public:
  TraceTraversal(Ids &ids) : ids(ids) { }

  template <typename Visitor>
  void traverse(TraceVisitor<Visitor> &visitor);

private:
  template <typename Visitor>
  void traverseFile(TraceVisitor<Visitor> &visitor, string traceFile);

  Ids &ids;
};

template <typename Visitor>
void TraceTraversal::traverse(TraceVisitor<Visitor> &visitor)
{
  if (traceFile != "") {
    traverseFile(visitor, traceFile);
  }
  else {
    TraceFiles traceFiles;
    for (TraceFiles::iterator it = traceFiles.begin(); it != traceFiles.end(); ++it) {
      traverseFile(visitor, *it);
    }
  }
}

template <typename Visitor>
void TraceTraversal::traverseFile(TraceVisitor<Visitor> &visitor, string traceFile)
{
  SUBCLASS(Visitor, visitor)->newTraceFile(traceFile);

  TraceEventsHandler<Visitor> handler(ids, visitor);
  TraceParser traces(traceFile);
  traces.parse(handler);
}

}

#endif

