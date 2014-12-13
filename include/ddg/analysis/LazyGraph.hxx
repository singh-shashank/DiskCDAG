/* LazyGraph.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef LAZYGRAPH_HXX
#define LAZYGRAPH_HXX

#include "ddg/analysis/Trace.hxx"

#include <llvm/ADT/DenseMap.h>
#include <llvm/Instructions.h>
#include <llvm/BasicBlock.h>
#include <llvm/Constants.h>
#include <llvm/Metadata.h>
#include <llvm/Support/CallSite.h>

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>

#include <stack>
#include <utility>
#include <algorithm>
#include <map>

namespace ddg
{
using namespace llvm;
using namespace boost;
using namespace std;

template <typename T> class LazyGraphBuilder;
template <typename T> class LazyGraph;


struct IVEntry
{
  enum Type { Loop, CallSite };
  typedef unsigned long int Value;
  Type type;
  Id id;
  Value value;
};


typedef vector<pair<IVEntry::Type, Id> > LoopNest;


class IterationVector
{
public:
  IterationVector(const vector<IVEntry> &indvars) : indvars(indvars)
  {
    loopNest.reserve(indvars.size());
    for (vector<IVEntry>::const_iterator it = indvars.begin(); it != indvars.end(); ++it) {
      loopNest.push_back(make_pair(it->type, it->id));
    }
  }

  IterationVector() { }

  vector<IVEntry>& getIndvars()
  {
    return indvars;
  }

  LoopNest& getLoopNest()
  {
    return loopNest;
  }


  bool getHighestCommonLoop(IterationVector& other,
                            Id &highestLoop /*out*/)
  {
    bool foundLoop = false;
    for (vector<IVEntry>::iterator i1 = indvars.begin(), i2 = other.indvars.begin();
         i1 != indvars.end() && i2 != other.indvars.end(); ++i1, ++i2) {
      if (i1->type != i2->type || i1->id != i2->id) {
        break;
      }
      foundLoop = true;
      highestLoop = i1->id;
    }
    return foundLoop;
  }


  bool getDifferingLoop(IterationVector& other,
                        Id &highestLoop /*out*/,
                        unsigned long int &ivValue /*out*/)
  {
    for (vector<IVEntry>::iterator i1 = indvars.begin(), i2 = other.indvars.begin();
         i1 != indvars.end() && i2 != other.indvars.end(); ++i1, ++i2) {
      if (i1->type != i2->type || i1->id != i2->id) {
        return false;
      }
    }
    for (vector<IVEntry>::iterator i1 = indvars.begin(), i2 = other.indvars.begin();
         i1 != indvars.end() && i2 != other.indvars.end(); ++i1, ++i2) {
      if (i1->value != i2->value) {
        highestLoop = i1->id;
        ivValue = i1->value;
        return true;
      }
    }
    return false;
  }

  bool getDifferingLoop(IterationVector& other,
                        Id &highestLoop /*out*/)
  {
    unsigned long int dummy;
    return getDifferingLoop(other, highestLoop, dummy);
  }

  void doSwap(IterationVector &other)
  {
    std::swap(indvars, other.indvars);
    std::swap(loopNest, other.loopNest);
  }

private:
  vector<IVEntry> indvars;
  LoopNest loopNest;
};


class LazyNodeBase
{
public:
  LazyNodeBase() : instruction(NULL) {}

  Instruction* getInstruction()
  {
    return instruction;
  }

  ExecutionId getExecutionId()
  {
    return executionId;
  }

  Address getAddress()
  {
    return address;
  }
  
  int getContextId()
  {
    return contextId;
  }

protected:
  LazyNodeBase(Instruction *instruction, ExecutionId executionId,
               Address address, int contextId)
    : instruction(instruction), executionId(executionId),
      address(address), contextId(contextId)
  {
  }

  void doSwap(LazyNodeBase &other)
  {
    swap(instruction, other.instruction);
    swap(executionId, other.executionId);
    swap(address, other.address);
    swap(contextId, other.contextId);
  }

private:
  Instruction *instruction;
  ExecutionId executionId;
  Address address;
  int contextId;
};

template <typename T=void>
class LazyNode : public LazyNodeBase
{
  template <typename U>
  friend class LazyGraph;


public:
  LazyNode() {}

  T& get()
  {
    return item;
  }

  void set(T &t)
  {
    item = t;
  }

private:
  LazyNode(Instruction *instruction, ExecutionId executionId,
           Address address, int contextId)
    : LazyNodeBase(instruction, executionId, address, contextId)
  {
  }

  void doSwap(LazyNode<T> &other)
  {
    LazyNodeBase::doSwap(other);
    swap(item, other.item);
  }

  T item;
};


template <>
class LazyNode<void> : public LazyNodeBase
{
  template <typename U>
  friend class LazyGraph;

public:
  LazyNode() {}

private:
  LazyNode(Instruction *instruction, ExecutionId executionId,
           Address address, int contextId)
    : LazyNodeBase(instruction, executionId, address, contextId)
  {
  }
};


template <typename T=void>
class LazyGraph
{
public:
  LazyGraph(Ids &ids) : ids(ids)
  {
    lastInstrWriters.resize(ids.getNumInsts());
  }

  LazyNode<T>* createNode(Instruction *instruction, ExecutionId executionId,
                          Address address, int contextId)
  {
    LazyNode<T> newNode(instruction, executionId, address, contextId);

    LazyNode<T> &lastWr = lastInstrWriters[ids.getId(instruction)];
    lastWr.doSwap(newNode);

    return &lastWr;
  }

  LazyNode<T>* createStoreNode(StoreInst *storeInst, ExecutionId executionId,
                               Address address, int contextId)
  {
    LazyNode<T> newNode(storeInst, executionId, address, contextId);

    LazyNode<T> &lastWr = lastAddrWriters[address];
    lastWr.doSwap(newNode);

    return &lastWr;
  }

  LazyNode<T>* getLastWriter(Instruction *instruction)
  {
    LazyNode<T> &lastWr = lastInstrWriters[ids.getId(instruction)];
    if (lastWr.getInstruction()) {
      return &lastWr;
    }
    else {
      return NULL;
    }
  }

  LazyNode<T>* getLastWriter(Address address)
  {
    typename LastAddrWriterMap::iterator f = lastAddrWriters.find(address);
    if (f != lastAddrWriters.end()) {
      return &f->second;
    }
    else {
      return NULL;
    }
  }

  void clear()
  {
    lastInstrWriters.clear();
    lastInstrWriters.resize(ids.getNumInsts());
    lastAddrWriters.clear();
  }

private:
  typedef unordered_map<Address,      LazyNode<T> > LastAddrWriterMap;

  vector<LazyNode<T> > lastInstrWriters;
  LastAddrWriterMap  lastAddrWriters;

  Ids &ids;
};


template <typename T=void>
class LazyGraphVisitor
{
  friend class LazyGraphBuilder<T>;
public:
  typedef T Payload;
  typedef SmallVector<LazyNode<T>*, 4> Predecessors;
  typedef SmallVector<int, 4> Operands;

  // TODO consider alternative api rather than using virtual polymorphism
  virtual void visitNode(LazyNode<T> *node,
                         Predecessors &predecessors,
                         Operands &operands) = 0;

  virtual void visitLoopBegin(int loopId) { }

  virtual void visitLoopEnd(int loopId) { }

  virtual void visitLoopIterBegin() { }

  virtual void visitLoopIterEnd() { }

  virtual void visitFnCall(Instruction *call) { }

  virtual void visitFnReturn(Instruction *call) { }

  void visit(Ids &ids);

protected:
  int getContextId(LoopNest &loopNest)
  {
    map<LoopNest, int>::iterator f = callContexts.find(loopNest);
    if (f == callContexts.end()) {
      f = callContexts.insert(make_pair(loopNest, callContexts.size()+1)).first;
    }
    return f->second;
  }

  map<LoopNest, int> callContexts;
};

template <typename T>
void LazyGraphVisitor<T>::visit(Ids &ids)
{
    LazyGraphBuilder<T> builder(*this, ids);

    TraceTraversal traversal(ids);
    std::cout << "starting lazygraph traverse\n";
    traversal.traverse(builder);
}

template <typename T=void>
class LazyGraphBuilder : public TraceVisitor<LazyGraphBuilder<T> >
{
public:
  typedef typename LazyGraphVisitor<T>::Predecessors Predecessors;
  typedef typename LazyGraphVisitor<T>::Operands Operands;

  LazyGraphBuilder(LazyGraphVisitor<T> &visitor, Ids &ids)
    : visitor(visitor), ids(ids), graph(ids)
  {
    currContextId = visitor.getContextId(loopNest);
  }

  void newTraceFile(string &fileName);
  void visitPHIs(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId, BasicBlock *prevBlock);
  void visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId);
  void visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId);
  void visitStore(StoreInst *storeInst, Address addr, ExecutionId executionId);
  void visitCall(Instruction *callInstr, ExecutionId executionId);
  void visitReturn(Instruction *call, ExecutionId callExecutionId, ReturnInst *ret, ExecutionId retExecutionId);
  void loopBegin(LoopBegin *loopBegin);
  void loopEnd(LoopEnd *loopEnd);
  void loopEnter(LoopEnter *loopEnter);
  void loopExit(LoopExit *loopExit);
  void loopIndVar(LoopIndVar *loopIndVar);

private:
  bool addLastWriterToPreds(Value *value, Predecessors &preds);

private:
  LazyGraph<T> graph;
  LazyGraphVisitor<T> &visitor;
  Ids &ids;

  stack<vector<Instruction*> > argStack;
  LoopNest loopNest;
  int currContextId;
};

template <typename T>
void LazyGraphBuilder<T>::newTraceFile(string &filename)
{
  graph.clear();
}

template <typename T>
void LazyGraphBuilder<T>::visitPHIs(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId, BasicBlock *prevBlock)
{
  for (BasicBlock::iterator it = begin; it != end; ++it) {
    Predecessors preds;
    Operands operands;
    PHINode *phi = cast<PHINode>(it);
    addLastWriterToPreds(phi->getIncomingValueForBlock(prevBlock), preds);
    LazyNode<T> *node = graph.createNode(phi, executionId, 0, currContextId);
    visitor.visitNode(node, preds, operands);
  }
}

template <typename T>
void LazyGraphBuilder<T>::visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId)
{
  for (BasicBlock::iterator it = begin; it != end; ++it) {
    if (!ids.isInstrumented(it)) {
      continue;
    }
    // TODO Add vector instructions
    if (it->isBinaryOp() || it->isCast() || isa<ReturnInst>(it) ||
        isa<GetElementPtrInst>(it)) {
      bool isCast = it->isCast();

      Predecessors preds;
      Operands operands;
      int opNo = 0;
      for (Instruction::op_iterator op = it->op_begin(), opEnd = it->op_end();
           op != opEnd;
           ++op, ++opNo) {
        if (addLastWriterToPreds(*op, preds)) {
          operands.push_back(opNo);
        }
      }

      Address addr = 0;
      if (isCast) {
        for (typename Predecessors::iterator it = preds.begin(); it != preds.end(); ++it) {
          addr = (*it)->getAddress();
        }
      }

      LazyNode<T> *node = graph.createNode(it, executionId, addr, currContextId);
      visitor.visitNode(node, preds, operands);
    }
    
    else if (isa<CallInst>(it) || isa<InvokeInst>(it)) {
      Predecessors preds;
      Operands operands;
      int opNo = 0;
      CallSite cs(it);
      for (CallSite::arg_iterator arg = cs.arg_begin(), argEnd = cs.arg_end();
           arg != argEnd; ++arg, ++opNo) {
        if (addLastWriterToPreds(*arg, preds)) {
          operands.push_back(opNo);
        }
      }

      LazyNode<T> *node = graph.createNode(it, executionId, 0, currContextId);
      visitor.visitNode(node, preds, operands);
    }

    else if (InsertValueInst *insertValue = dyn_cast<InsertValueInst>(it)) {
      Predecessors preds;
      Operands operands;
      if (addLastWriterToPreds(insertValue->getAggregateOperand(), preds)) {
        operands.push_back(0);
      }
      if (addLastWriterToPreds(insertValue->getInsertedValueOperand(), preds)) {
        operands.push_back(1);
      }

      LazyNode<T> *node = graph.createNode(it, executionId, 0, currContextId);
      visitor.visitNode(node, preds, operands);
    }

    else if (ExtractValueInst *extractValue = dyn_cast<ExtractValueInst>(it)) {
      Predecessors preds;
      Operands operands;
      if (addLastWriterToPreds(extractValue->getAggregateOperand(), preds)) {
        operands.push_back(0);
      }

      LazyNode<T> *node = graph.createNode(it, executionId, 0, currContextId);
      visitor.visitNode(node, preds, operands);
    }

  }
}

template <typename T>
void LazyGraphBuilder<T>::visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId)
{
  Predecessors preds;
  Operands operands;
  if (LazyNode<T> *pred = graph.getLastWriter(addr)) {
    preds.push_back(pred);
    operands.push_back(0);
  }
  if (addLastWriterToPreds(loadInst->getPointerOperand(), preds)) {
    operands.push_back(1);
  }
  LazyNode<T> *node = graph.createNode(loadInst, executionId, addr, currContextId);
  visitor.visitNode(node, preds, operands);
}

template <typename T>
void LazyGraphBuilder<T>::visitStore(StoreInst *storeInst, Address addr, ExecutionId executionId)
{
  Predecessors preds;
  Operands operands;
  if (addLastWriterToPreds(storeInst->getValueOperand(), preds)) {
    operands.push_back(0);
  }
  if (addLastWriterToPreds(storeInst->getPointerOperand(), preds)) {
    operands.push_back(1);
  }
  LazyNode<T> *node = graph.createStoreNode(storeInst, executionId, addr, currContextId);
  visitor.visitNode(node, preds, operands);
}

template <typename T>
void LazyGraphBuilder<T>::visitCall(Instruction *callInstr, ExecutionId executionId)
{
  CallSite callSite(callInstr);
  vector<Instruction*> args;
  for (CallSite::arg_iterator argIt = callSite.arg_begin(), argEnd = callSite.arg_end();
       argIt != argEnd; ++argIt) {
    Value *argVal = *argIt;
    if (Argument *arg = dyn_cast<Argument>(argVal)) {
      args.push_back(!argStack.empty() ? argStack.top()[arg->getArgNo()] : NULL);
    }
    else if (Instruction *instr = dyn_cast<Instruction>(argVal)){
      args.push_back(instr);
    }
    else {
      args.push_back(NULL);
    }
  }
  argStack.push(vector<Instruction*>());
  swap(args, argStack.top());

  if (ids.isCallSite(callInstr)) {
    Id callId = ids.getId(callInstr);
    loopNest.push_back(make_pair(IVEntry::CallSite, callId));
    currContextId = visitor.getContextId(loopNest);
  }

  visitor.visitFnCall(callInstr);
}

template <typename T>
void LazyGraphBuilder<T>::visitReturn(Instruction *call, ExecutionId callExecutionId, ReturnInst *ret, ExecutionId retExecutionId)
{
  argStack.pop();
  if (ids.isCallSite(call)) {
    loopNest.pop_back();
    currContextId = visitor.getContextId(loopNest);
  }

  visitor.visitFnReturn(call);

  Predecessors preds;
  Operands operands;

  if (ret && ret->getReturnValue()) {
    addLastWriterToPreds(ret->getReturnValue(), preds);
  }
  LazyNode<T> *node = graph.createNode(call, callExecutionId, 0, currContextId);
  visitor.visitNode(node, preds, operands);
}

template <typename T>
void LazyGraphBuilder<T>::loopBegin(LoopBegin *loopBegin)
{
  loopNest.push_back(make_pair(IVEntry::Loop, loopBegin->getLoopId()));
  currContextId = visitor.getContextId(loopNest);
  visitor.visitLoopBegin(loopBegin->getLoopId());
}

template <typename T>
void LazyGraphBuilder<T>::loopEnd(LoopEnd *loopEnd)
{
  loopNest.pop_back();
  currContextId = visitor.getContextId(loopNest);
  visitor.visitLoopEnd(loopEnd->getLoopId());
}

template <typename T>
void LazyGraphBuilder<T>::loopEnter(LoopEnter *loopEnter)
{
  visitor.visitLoopIterBegin();
}

template <typename T>
void LazyGraphBuilder<T>::loopExit(LoopExit *loopExit)
{
  visitor.visitLoopIterEnd();
}

template <typename T>
void LazyGraphBuilder<T>::loopIndVar(LoopIndVar *loopIndVar)
{
}

template <typename T>
bool LazyGraphBuilder<T>::addLastWriterToPreds(Value *value, Predecessors &preds)
{
  if (Instruction *instr = dyn_cast<Instruction>(value)) {
    if (LazyNode<T> *pred = graph.getLastWriter(instr)) {
      preds.push_back(pred);
      return true;
    }
  }
  else if (Argument *arg = dyn_cast<Argument>(value)) {
    if (argStack.empty()) {
      return false;
    }
    if (Instruction *instr = argStack.top()[arg->getArgNo()]) {
      if (LazyNode<T> *pred = graph.getLastWriter(instr)) {
        preds.push_back(pred);
        return true;
      }
    }
  }
  return false;
}

}

#endif

