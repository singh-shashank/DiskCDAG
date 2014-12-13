/* Ids.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef IDS_HXX
#define IDS_HXX

#include "ddg/common/types.hxx"

#include <llvm/Pass.h>
#include <llvm/Instruction.h>
#include <llvm/BasicBlock.h>
#include <llvm/Function.h>
#include <llvm/ADT/DenseMap.h>

#include <vector>
#include <deque>
#include <set>

namespace ddg
{

  using namespace llvm;
  using namespace std;

  class Ids : public ModulePass
  {
    friend class IdsVisitor;

    struct InstrInfo {
      Id id;
      Id isInstrumented;
    };

  public:
    static char ID;

    Ids() : ModulePass(ID) {}
    virtual bool runOnModule(Module &module);
    virtual void getAnalysisUsage(AnalysisUsage &usage) const;

    inline Instruction* getInstruction(Id id)
    {
      return instructions[id];
    }

    inline Id getId(Instruction *instr)
    {
      return ((InstrInfo*)&instr->getDebugLoc())->id;
    }

    inline int getNumInsts()
    {
      return instructions.size();
    }

    inline bool isInstrumented(Instruction *instr)
    {
      return ((InstrInfo*)&instr->getDebugLoc())->isInstrumented;
    }

    inline bool isCallSite(Instruction *instr)
    {
      return callIds.count(instr);
    }

    inline BasicBlock* getBasicBlock(Id id)
    {
      return basicBlocks[id];
    }

    inline Function* getFunction(Id id)
    {
      return functions[id];
    }

    inline BasicBlock* getLoop(Id id)
    {
      return loops[id];
    }

    inline int getNumLoops()
    {
      return loops.size();
    }

    inline BasicBlock::iterator getFirstNonPHI(Id bbId)
    {
      return firstNonPHIs[bbId];
    }

    inline BasicBlock::iterator getFirstInsertionPoint(Id bbId)
    {
      return firstInsertionPoints[bbId];
    }

    inline bool isFirstPHI(Id bbId)
    {
      return isFirstPHIs[bbId];
    }

  private:
    vector<Instruction*> instructions;
    vector<BasicBlock*> basicBlocks;
    vector<Function*> functions;
    vector<BasicBlock*> loops;

    DenseMap<Instruction*, Id> callIds;

    vector<BasicBlock::iterator> firstNonPHIs;
    vector<BasicBlock::iterator> firstInsertionPoints;
    vector<bool> isFirstPHIs;
  };

}

#endif

