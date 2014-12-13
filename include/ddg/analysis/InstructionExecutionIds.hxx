/* InstructionExecutionIds.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef INSTRUCTIONEXECUTIONIDS_HXX
#define INSTRUCTIONEXECUTIONIDS_HXX

#include "ddg/common/types.hxx"

#include <llvm/ADT/ValueMap.h>
#include <llvm/Support/raw_ostream.h>

#include <vector>

namespace ddg
{

template<typename K, typename V>
class ValueMapStack
{
private:
  typedef ValueMap<K, V> map_type;
  typedef vector<map_type*> stack_type;

  stack_type mapStack;

public:
  typedef typename map_type::iterator iterator;

  ValueMapStack()
  {
    push();
  }

  void insert(pair<K, V> inp)
  {
    mapStack.back()->erase(inp.first);
    mapStack.back()->insert(inp);
  }

  iterator end()
  {
    return mapStack.front()->end();
  }

  iterator find(K k)
  {
    for (typename stack_type::iterator it = mapStack.begin(); it != mapStack.end(); it++) {
      iterator el = (*it)->find(k);
      if (el != (*it)->end()) {
        return el;
      }
    }
    return end();
  }

  V operator[](K k)
  {
    return find(k)->second;
  }

  void push()
  {
    mapStack.push_back(new map_type());
  }

  void pop()
  {
    map_type* back = mapStack.back();
    mapStack.pop_back();
    delete back;
  }

  ~ValueMapStack()
  {
    for (typename stack_type::iterator it = mapStack.begin(); it != mapStack.end(); it++) {
      delete *it;
    }
  }
};

class InstructionExecutionIds
{
public:
  InstructionExecutionIds() : executionId(-1) { } 

  void basicBlockEnter(BasicBlock *basicBlock)
  {
    executionId++;
    bbLatestExecutionIds.insert(make_pair(basicBlock, executionId));
  }

  void fnCall()
  {
    bbLatestExecutionIds.push();
  }

  void fnReturn()
  {
    bbLatestExecutionIds.pop();
  }

  bool wasVisited(Instruction *instruction)
  {
    return bbLatestExecutionIds.find(instruction->getParent()) != bbLatestExecutionIds.end();
  }

  bool wasVisited(BasicBlock *basicBlock)
  {
    return bbLatestExecutionIds.find(basicBlock) != bbLatestExecutionIds.end();
  }

  ExecutionId getLastExecutionId(Instruction *instruction)
  {
    BasicBlockExecutionIds::iterator f = bbLatestExecutionIds.find(instruction->getParent());
    return f == bbLatestExecutionIds.end() ? -1 : f->second;
  }
  
  ExecutionId getLastExecutionId(BasicBlock *basicBlock)
  {
    BasicBlockExecutionIds::iterator f = bbLatestExecutionIds.find(basicBlock);
    return f == bbLatestExecutionIds.end() ? -1 : f->second;
  }

  ExecutionId getNextExecutionId()
  {
    return executionId+1;
  }

private:
  typedef ValueMapStack<BasicBlock*, ExecutionId> BasicBlockExecutionIds;

  BasicBlockExecutionIds bbLatestExecutionIds;
  ExecutionId executionId;
};

};

#endif

