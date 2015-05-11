/* InstrumentDDG.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include <iostream>

#include "ddg/instrument/Visitors.hxx"

#include <llvm/Pass.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Support/CFG.h>
#include <llvm/Support/TypeBuilder.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Assembly/Writer.h>

#include <set>
#include <algorithm>

using namespace llvm;
using namespace std;

namespace ddg
{

class InstrumentDDG : public ModulePass
{
public:
  static char ID;

  InstrumentDDG() : ModulePass(ID) {}
  virtual bool runOnModule(Module &module);
  virtual void getAnalysisUsage(AnalysisUsage &usage) const;

private:
  void splitAtUse(Function *fn);
};

char InstrumentDDG::ID = 0;

bool InstrumentDDG::runOnModule(Module &module)
{
  LLVMContext &context = module.getContext();

  Function *initFn = cast<Function>(module.getOrInsertFunction("ddg_init",
      TypeBuilder<void(), true>::get(module.getContext())));
  Function *cleanupFn = cast<Function>(module.getOrInsertFunction("ddg_cleanup",
      TypeBuilder<void(), true>::get(module.getContext())));
  appendToGlobalCtors(module, initFn, 1);
  appendToGlobalDtors(module, cleanupFn, 1);

  Function *startTraceFn = module.getFunction("ddg_start_trace");
  if (!startTraceFn) {
    startTraceFn = module.getFunction("ddg_start_trace_");
  }
  if (startTraceFn) {
    splitAtUse(startTraceFn);
  }

  Function *stopTraceFn  = module.getFunction("ddg_stop_trace");
  if (!stopTraceFn) {
    stopTraceFn = module.getFunction("ddg_stop_trace_");
  }
  if (stopTraceFn) {
    splitAtUse(stopTraceFn);
  }
  else
  {
    std::cout << "\n ddg_start_trace && ddg_ stop_trace markers are missing...exiting!";
    return true;
  }


  NumberingVisitor numberingVisitor(context, startTraceFn, stopTraceFn);
  numberingVisitor.visit(module);

  BBVisitor bbVisitor(context);
  bbVisitor.visit(module);

  CallVisitor callVisitor(context);
  callVisitor.visit(module);

  MemoryInstVisitor memInstVisitor(context);
  memInstVisitor.visit(module);

  LoopVisitor loopVisitor(*this, context);
  loopVisitor.visit(module);

  FnVisitor fnVisitor(context);
  fnVisitor.visit(module);

  return true;
}

void InstrumentDDG::getAnalysisUsage(AnalysisUsage &usage) const
{
  usage.addRequired<LoopInfo>();
}

static RegisterPass<InstrumentDDG> instrumentDdg("instrument-ddg", "DDG: Instrument instructions", false, false);

void InstrumentDDG::splitAtUse(Function *fn)
{
  for (Function::use_iterator u = fn->use_begin(), uEnd = fn->use_end();
       u != uEnd; ++u) {
    Value *use = *u;
    if (isa<ConstantExpr>(use)) {
      for (Function::use_iterator uu = use->use_begin(), uuEnd = use->use_end();
           uu != uuEnd; ++uu) {
          use = *uu;

        if (CallInst *call = dyn_cast<CallInst>(use)) {
          BasicBlock::iterator it = call;
          ++it;

          SplitBlock(call->getParent(), it, this);
        }
      }
    }
    else {
      if (CallInst *call = dyn_cast<CallInst>(use)) {
        BasicBlock::iterator it = call;
        ++it;

        SplitBlock(call->getParent(), it, this);
      }
    }
  }
}

class InstrumentLoopIndVars : public LoopPass
{
public:
  static char ID;

  InstrumentLoopIndVars() : LoopPass(ID) {}
  virtual bool runOnLoop(Loop *loop, LPPassManager &lpm);
  virtual void getAnalysisUsage(AnalysisUsage &usage) const;
};

char InstrumentLoopIndVars::ID = 0;

bool InstrumentLoopIndVars::runOnLoop(Loop *loop, LPPassManager &lpm)
{
  BasicBlock *header = loop->getHeader();
  Instruction *first = header->getFirstInsertionPt();

  Value *loopId = first->getMetadata("loopId")->getOperand(0);
  
  Module *module = header->getParent()->getParent();
  LLVMContext &context = module->getContext();

  if (PHINode *iv = loop->getCanonicalInductionVariable()) {
    MDNode* canIvMD = MDNode::get(context,
        UndefValue::get(TypeBuilder<llvm::types::i<1>, true>::get(context)));
    iv->setMetadata("canIv", canIvMD);

    Value *instId = iv->getMetadata("id")->getOperand(0);
    IRBuilder<> builder(first);

    if (iv->getType() == Type::getInt32Ty(context)) {
      Constant *loopIndVar32Fn = module->getOrInsertFunction("ddg_loop_indvar32",
        TypeBuilder<void(llvm::types::i<32>, llvm::types::i<32>), true>::get(context));
      builder.CreateCall2(loopIndVar32Fn, instId, iv);
    }
    else if (iv->getType() == Type::getInt64Ty(context)) {
      Constant *loopIndVar64Fn = module->getOrInsertFunction("ddg_loop_indvar64",
        TypeBuilder<void(llvm::types::i<32>, llvm::types::i<64>), true>::get(context));
      builder.CreateCall2(loopIndVar64Fn, instId, iv);
    }
  }

  Constant *loopEnterFn = module->getOrInsertFunction("ddg_loop_enter",
      TypeBuilder<void(types::i<32>), true>::get(context));
  Constant *loopExitFn = module->getOrInsertFunction("ddg_loop_exit",
      TypeBuilder<void(types::i<32>), true>::get(context));

  IRBuilder<> enterBuilder(header->getFirstInsertionPt());
  CallInst *call = enterBuilder.CreateCall(loopEnterFn, loopId);
  call->setMetadata("bbId", first->getMetadata("bbId"));
  call->setMetadata("loopId", first->getMetadata("loopId"));

  for (pred_iterator pIt = pred_begin(header), pEnd = pred_end(header);
       pIt != pEnd; ++pIt) {
    if (loop->contains(*pIt)) {
      IRBuilder<> exitBuilder((*pIt)->getTerminator());
      exitBuilder.CreateCall(loopExitFn, loopId);
    }
  }

  return true;
}

void InstrumentLoopIndVars::getAnalysisUsage(AnalysisUsage &usage) const
{
  usage.setPreservesAll();
}

static RegisterPass<InstrumentLoopIndVars> instrumentLoopIndVars("instrument-indvars", "DDG: Instrument loop indvars", false, false);

class InstrumentWholeProgram : public ModulePass
{
public:
  static char ID;

  InstrumentWholeProgram() : ModulePass(ID) {}
  virtual bool runOnModule(Module &module);
  virtual void getAnalysisUsage(AnalysisUsage &usage) const;

};

char InstrumentWholeProgram::ID = 0;

bool InstrumentWholeProgram::runOnModule(Module &module)
{
  if (Function *mainFn = module.getFunction("main")) {
    LLVMContext &context = module.getContext();
    Constant *startFn = module.getFunction("ddg_start_trace");

    if (!startFn) {
      startFn = module.getOrInsertFunction("ddg_start_trace", TypeBuilder<void(), true>::get(context));
    }

    IRBuilder<> builder(mainFn->getEntryBlock().getFirstInsertionPt());
    builder.CreateCall(startFn);

    return true;
  }
  return false;
}

void InstrumentWholeProgram::getAnalysisUsage(AnalysisUsage &usage) const
{
  usage.setPreservesAll();
}

static RegisterPass<InstrumentWholeProgram> instrumentWholeProgram("instrument-whole-program", "DDG: Instrument whole program", false, false);


class SplitLPads : public FunctionPass
{
public:
  static char ID;

  SplitLPads() : FunctionPass(ID) {}
  virtual bool runOnFunction(Function &function);

private:
  void splitAtUse(Function *fn);
};

char SplitLPads::ID = 0;

bool SplitLPads::runOnFunction(Function &function)
{
  bool changed = false;
  // The goal of this pass is to transorm the code such that all invokes have
  // an unique normal and unwind destination.


  // If there are more than one predecessor for a landingpad, create as many
  // separate landing pads as there are predecessors.
  // This transformations helps us determine which invoke was executed prior to
  // a landingpad.
  for (Function::iterator bb = function.begin(); bb != function.end(); ++bb) {
    if (LandingPadInst *lpadInst = dyn_cast<LandingPadInst>(bb->begin())) {
      int c = distance(pred_begin(bb), pred_end(bb));
      if (c > 1) {
        BasicBlock* splitBB = SplitBlock(bb, lpadInst, this);
        PHINode *phi = PHINode::Create(lpadInst->getType(), c, "", splitBB->begin());
        lpadInst->replaceAllUsesWith(phi);
        phi->addIncoming(lpadInst, bb);

        vector<BasicBlock*> preds;
        copy(pred_begin(bb), pred_end(bb), back_inserter(preds));

        vector<BasicBlock*>::iterator it = preds.begin();
        ++it;
        for (; it != preds.end(); ++it) {
          InvokeInst *invoke = cast<InvokeInst>((*it)->getTerminator());

          ValueToValueMapTy VMap;
          BasicBlock* newBB = CloneBasicBlock(bb, VMap, ".sl");
          function.getBasicBlockList().insert(bb, newBB);

          invoke->setUnwindDest(newBB);
          phi->addIncoming(VMap[lpadInst], newBB);
        }

        changed = true;
      }
    }
  }

  // For each invoke, create a new basic block in the path between the invoke and
  // its normal destination.
  // This transformation ensures that each invoke has an unique normal destination 
  // which helps us to correctly instrument function return event.
  for (Function::iterator bb = function.begin(); bb != function.end(); ++bb) {
    if (InvokeInst *invoke = dyn_cast<InvokeInst>(bb->getTerminator())) {
      BasicBlock *dest = invoke->getNormalDest();
      BasicBlock *newBB = BasicBlock::Create(function.getContext(), "", &function, dest);
      BranchInst::Create(dest, newBB);
      invoke->setNormalDest(newBB);

      for (BasicBlock::iterator phis = dest->begin();
           phis != dest->end() && isa<PHINode>(phis); ++phis) {
        PHINode *phi = cast<PHINode>(phis);
        phi->addIncoming(phi->removeIncomingValue(bb), newBB);
      }
      changed = true;
    }
  }

  return changed;
}

static RegisterPass<SplitLPads> splitLPads("split-lpads", "DDG: Split lpads");

}

