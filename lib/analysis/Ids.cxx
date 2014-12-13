/* Ids.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/Ids.hxx"

#include <llvm/Constants.h>
#include <llvm/Support/InstVisitor.h>

namespace ddg
{

using namespace llvm;
using namespace std;

class IdsVisitor : public InstVisitor<IdsVisitor, void>
{
public:
  IdsVisitor(Ids &ids)
    : ids(ids) {}

  void visitInstruction(Instruction &instr)
  {
    if (MDNode* metadata = instr.getMetadata("id")) {
      Id id = cast<ConstantInt>(metadata->getOperand(0))->getZExtValue();
      if (id == ids.instructions.size()) {
	ids.instructions.push_back(&instr);
      }
      DebugLoc loc;
      Ids::InstrInfo* o = (Ids::InstrInfo*)&loc;
      o->id = id;
      o->isInstrumented = 1;
      instr.setDebugLoc(loc);
    }
    else {
      DebugLoc loc;
      Ids::InstrInfo* o = (Ids::InstrInfo*)&loc;
      o->id = 0;
      o->isInstrumented = 0;
      instr.setDebugLoc(loc);
    }

    if (MDNode* metadata = instr.getMetadata("callId"))
      ids.callIds.insert(make_pair(&instr,
                cast<ConstantInt>(metadata->getOperand(0))->getZExtValue()));
  }

  void visitBasicBlock(BasicBlock &basicBlock)
  {
    Instruction* first = basicBlock.getFirstInsertionPt();
    if (MDNode* metadata = first->getMetadata("bbId"))
      if (ConstantInt* id = dyn_cast<ConstantInt>(metadata->getOperand(0)))
        if (id->getZExtValue() == ids.basicBlocks.size()) {
          ids.basicBlocks.push_back(&basicBlock);
          ids.firstNonPHIs.push_back(basicBlock.getFirstNonPHI());
          ids.firstInsertionPoints.push_back(basicBlock.getFirstInsertionPt());
          ids.isFirstPHIs.push_back(isa<PHINode>(basicBlock.begin()));
        }

    if (MDNode* metadata = first->getMetadata("loopId"))
      if (ConstantInt* id = dyn_cast<ConstantInt>(metadata->getOperand(0)))
        if (id->getZExtValue() == ids.loops.size())
          ids.loops.push_back(&basicBlock);
  }

  void visitFunction(Function &fn)
  {
    if (fn.isDeclaration()) {
      return;
    }

    Instruction *first = fn.getEntryBlock().getFirstInsertionPt();
    if (MDNode* metadata = first->getMetadata("fnId")) 
      if (ConstantInt* id = dyn_cast<ConstantInt>(metadata->getOperand(0)))
        if (id->getZExtValue() == ids.functions.size())
          ids.functions.push_back(&fn);
  }

private:
  Ids &ids;
};

char Ids::ID = 0;

bool Ids::runOnModule(Module &module)
{
  IdsVisitor visitor(*this);
  visitor.visit(module);
  return false;
}

void Ids::getAnalysisUsage(AnalysisUsage &usage) const
{
  usage.setPreservesAll();
}


static RegisterPass<Ids> ids("ids", "Get ids", false, true);

}

