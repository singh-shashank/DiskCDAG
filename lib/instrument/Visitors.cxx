/* Visitors.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/instrument/Visitors.hxx"

#include <llvm/Intrinsics.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include <vector>
#include <utility>
#include <cassert>

namespace ddg
{

using namespace std;

void NumberingVisitor::visitCallInst(CallInst &callInst)
{
  Function *fn = dyn_cast<Function>(callInst.getCalledValue()->stripPointerCasts());
  if (   (startTraceFn && fn == startTraceFn) 
      || (stopTraceFn && fn == stopTraceFn)
      || (regionBeginFn && fn == regionBeginFn)
      || (regionEndFn && fn == regionEndFn)) {
    return;
  }
  visitInstruction(callInst);
}

void NumberingVisitor::visitInstruction(Instruction &instruction)
{
  Value *mdValues = ConstantInt::get(int32Ty, nextInstrId);
  instruction.setMetadata("id", MDNode::get(context, mdValues));
  ++nextInstrId;
}

void NumberingVisitor::setBeginEndRegionFunction(Constant *regionBeginFn, Constant *regionEndFn)
{
  this->regionBeginFn = regionBeginFn;
  this->regionEndFn = regionEndFn;
}

void BBVisitor::visitModule(Module &module)
{
  bbEnterFn = module.getOrInsertFunction("ddg_basic_block_enter",
      TypeBuilder<void(llvm::types::i<32>), true>::get(module.getContext()));
  fnRetFn = module.getOrInsertFunction("ddg_function_ret",
      TypeBuilder<void(types::i<32>), true>::get(context));
}

void BBVisitor::visitBasicBlock(BasicBlock &basicBlock)
{
  Value *blockId = ConstantInt::get(int32Ty, nextBlockId);
  MDNode *metadata = MDNode::get(context, blockId);

  IRBuilder<> builder(basicBlock.getFirstInsertionPt());
  builder.CreateCall(bbEnterFn, blockId)->setMetadata("bbId", metadata);

  ++nextBlockId;
}

void MemoryInstVisitor::visitModule(Module &module)
{
  loadFn = module.getOrInsertFunction("ddg_load",
      TypeBuilder<void(types::i<32>, types::i<8>*), true>::get(context));
  storeFn = module.getOrInsertFunction("ddg_store",
      TypeBuilder<void(types::i<32>, types::i<8>*), true>::get(context));
}

void MemoryInstVisitor::visitLoadInst(LoadInst &loadInst)
{
  IRBuilder<> builder(&loadInst);
  Value *castInst = builder.CreateBitCast(loadInst.getPointerOperand(), int8PtrTy);
  builder.CreateCall2(loadFn, loadInst.getMetadata("id")->getOperand(0), castInst);
}

void MemoryInstVisitor::visitStoreInst(StoreInst &storeInst)
{
  IRBuilder<> builder(&storeInst);
  Value* castInst = builder.CreateBitCast(storeInst.getPointerOperand(), int8PtrTy);
  builder.CreateCall2(storeFn,
      storeInst.getMetadata("id")->getOperand(0),
      castInst);
}


class CallSiteNumberingVisitor : public InstVisitor<CallSiteNumberingVisitor, void>
{
public:
  CallSiteNumberingVisitor(LLVMContext &context) : context(context), nextCallSiteId(0) {}

  void visitCallInst(CallInst &callInst)
  {
    if (!isa<IntrinsicInst>(callInst)) {
      tagCallSite(callInst);
    }
  }

  void visitInvokeInst(InvokeInst &invokeInst)
  {
    tagCallSite(invokeInst);
  }

private:

  void tagCallSite(Instruction &instruction)
  {
    if (instruction.getMetadata("id")) {
      Value *callSiteId = ConstantInt::get(Type::getInt32Ty(context), nextCallSiteId);
      instruction.setMetadata("callId", MDNode::get(context, callSiteId));
      ++nextCallSiteId;
    }
  }

  int nextCallSiteId;
  LLVMContext &context;
};


void LoopVisitor::visitModule(Module &module)
{
  loopBeginFn = module.getOrInsertFunction("ddg_loop_begin", TypeBuilder<void(types::i<32>), true>::get(context));
  loopEndFn = module.getOrInsertFunction("ddg_loop_end", TypeBuilder<void(types::i<32>), true>::get(context));
}

void LoopVisitor::visitFunction(Function &function)
{
  if (function.isDeclaration()) {
    return;
  }
  loopInfo = &pass.getAnalysis<LoopInfo>(function);
}

void LoopVisitor::visitBasicBlock(BasicBlock &basicBlock)
{
  if (loopInfo->isLoopHeader(&basicBlock)) {
    Loop *loop = loopInfo->getLoopFor(&basicBlock);

    if (loop->getLoopDepth() == 1) {
      for (po_iterator<Loop*> pIt = po_begin(loop), pEnd = po_end(loop);
           pIt != pEnd; ++pIt) {
        Loop *l = *pIt;
        CallSiteNumberingVisitor callSiteVisitor(context);
        for (Loop::block_iterator bIt = l->block_begin(), bEnd = l->block_end();
             bIt != bEnd; ++bIt) {
          if (loopInfo->getLoopDepth(*bIt) == l->getLoopDepth()) {
            callSiteVisitor.visit(*bIt);
          }
        }
      }
    }

    Value *loopId = ConstantInt::get(int32Ty, nextLoopId);
    basicBlock.getFirstInsertionPt()->setMetadata("loopId", MDNode::get(context, loopId));

    BasicBlock *preHeader = loop->getLoopPreheader();
    IRBuilder<> beginBuilder(preHeader->getTerminator());
    beginBuilder.CreateCall(loopBeginFn, loopId);

    SmallVector<BasicBlock*, 4> exits;
    SmallSet<BasicBlock*, 4> uniqueExits;
    loop->getExitBlocks(exits);
    for (SmallVector<BasicBlock*, 4>::iterator it = exits.begin(); it != exits.end(); ++it) {
      BasicBlock *bb = *it;

      if (uniqueExits.count(bb)) {
        continue;
      }
      uniqueExits.insert(bb);

      Instruction *first = bb->getFirstInsertionPt();
      IRBuilder<> endBuilder(first);
      endBuilder.CreateCall(loopEndFn, loopId)->setMetadata("bbId", first->getMetadata("bbId"));
    }
    nextLoopId++;
  }
}

void CallVisitor::visitModule(Module &module)
{
  fnCallFn = module.getOrInsertFunction("ddg_function_call",
      TypeBuilder<void(types::i<32>, types::i<8>*), true>::get(context));
  fnRetFn = module.getOrInsertFunction("ddg_function_ret",
      TypeBuilder<void(types::i<32>), true>::get(context));
}

void CallVisitor::visitCallInst(CallInst &callInst)
{
  visitInstruction(callInst);

  if (callInst.getMetadata("id") == NULL) {
    return;
  }

  Function *fn = callInst.getCalledFunction();
  if ((fn && fn->isIntrinsic()) || callInst.doesNotReturn()) {
    return;
  }

  IRBuilder<> builder(&callInst);
  builder.CreateCall2(fnCallFn,
      callInst.getMetadata("id")->getOperand(0),
      builder.CreateBitCast(callInst.getCalledValue(), TypeBuilder<types::i<8>*, true>::get(context)));
  prevCall = &callInst;
}

void CallVisitor::visitInvokeInst(InvokeInst &invokeInst)
{
  visitInstruction(invokeInst);

  if (invokeInst.getMetadata("id") == NULL) {
    return;
  }

  Function *fn = invokeInst.getCalledFunction();
  IRBuilder<> builder(&invokeInst);
  builder.CreateCall2(fnCallFn,
      invokeInst.getMetadata("id")->getOperand(0),
      builder.CreateBitCast(invokeInst.getCalledValue(), TypeBuilder<types::i<8>*, true>::get(context)));
  prevInvoke = &invokeInst;
}

void CallVisitor::addFnRetCall(InvokeInst *invoke, BasicBlock *dest)
{
  IRBuilder<> builder(dest->getFirstInsertionPt());
  builder.CreateCall(fnRetFn, invoke->getMetadata("id")->getOperand(0))
    ->setMetadata("bbId", dest->getFirstInsertionPt()->getMetadata("bbId"));
}

void CallVisitor::visitInstruction(Instruction &instruction)
{
  if (prevCall) {
    IRBuilder<> builder(&instruction);
    builder.CreateCall(fnRetFn, prevCall->getMetadata("id")->getOperand(0));
    prevCall = NULL;
  }
  if (prevInvoke) {
    BasicBlock *normalDest = prevInvoke->getNormalDest();
    addFnRetCall(prevInvoke, normalDest);

    BasicBlock *unwindDest = prevInvoke->getUnwindDest();
    addFnRetCall(prevInvoke, unwindDest);

    prevInvoke = NULL;
  }
}

FnVisitor::FnVisitor(LLVMContext &context) : context(context), nextFnId(0)
{
}

void FnVisitor::visitModule(Module &module)
{
  fnEnterFn = module.getOrInsertFunction("ddg_function_enter",
      TypeBuilder<void(types::i<32>, types::i<8>*), true>::get(context));
}

void FnVisitor::visitFunction(Function &fn)
{
  if (fn.isDeclaration()) {
    return;
  }

  Value * const fnId = ConstantInt::get(
      TypeBuilder<types::i<32>, true>::get(context), nextFnId);

  Instruction *first = fn.getEntryBlock().getFirstInsertionPt();
  IRBuilder<> builder(first);
  CallInst* call = builder.CreateCall2(fnEnterFn,
      fnId,
      builder.CreateBitCast(&fn, TypeBuilder<types::i<8>*, true>::get(context)));
  call->setMetadata("fnId", MDNode::get(context, fnId));
  call->setMetadata("bbId", first->getMetadata("bbId"));

  nextFnId++;
}

} 

