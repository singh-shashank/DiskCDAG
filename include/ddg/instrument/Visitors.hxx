/* Visitors.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef VISITORS_HXX
#define VISITORS_HXX

#include "ddg/common/types.hxx"

#include <llvm/Pass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/InstVisitor.h>
#include <llvm/Support/TypeBuilder.h>

#include <set>

namespace ddg
{

using namespace llvm;
using namespace std;

class NumberingVisitor : public InstVisitor<NumberingVisitor, void>
{
public:
  NumberingVisitor(LLVMContext &context, Constant *startTraceFn, Constant *stopTraceFn)
    : context(context), nextInstrId(0), int32Ty(Type::getInt32Ty(context)), 
      startTraceFn(startTraceFn), stopTraceFn(stopTraceFn)
  { }

  void visitCallInst(CallInst &callInst);
  void visitInstruction(Instruction &instruction);

private:
  LLVMContext &context;
  Type* int32Ty;

  Constant *startTraceFn;
  Constant *stopTraceFn;

  Id nextInstrId;
};

class BBVisitor : public InstVisitor<BBVisitor, void>
{
public:
  BBVisitor(LLVMContext &context)
    : context(context), nextBlockId(0), int32Ty(Type::getInt32Ty(context)) { }

  void visitModule(Module &module);
  void visitBasicBlock(BasicBlock &basicBlock);

private:
  LLVMContext &context;

  Type* int32Ty;
  Constant *bbEnterFn;
  Constant *fnRetFn;

  Id nextBlockId;
};

class MemoryInstVisitor : public InstVisitor<MemoryInstVisitor, void>
{
public:
  MemoryInstVisitor(LLVMContext &context) :
    context(context),
    int32Ty(TypeBuilder<types::i<32>, true>::get(context)),
    int8PtrTy(TypeBuilder<types::i<8>*, true>::get(context))
  { }

  void visitModule(Module &module);
  void visitLoadInst(LoadInst &loadInst);
  void visitStoreInst(StoreInst &storeInst);

private:
  LLVMContext &context;

  Type* int32Ty;
  Type* int8PtrTy;
  Constant* loadFn;
  Constant* storeFn;
};

class LoopVisitor : public InstVisitor<LoopVisitor, void>
{
public:
  LoopVisitor(ModulePass &pass, LLVMContext &context)
    : pass(pass), context(context), int32Ty(TypeBuilder<types::i<32>, true>::get(context)), nextLoopId(0)
  { }

  void visitModule(Module &module);
  void visitFunction(Function &function);
  void visitBasicBlock(BasicBlock &basicBlock);

private:
  ModulePass &pass;
  LLVMContext &context;

  Type* int32Ty;

  Id nextLoopId;

  LoopInfo *loopInfo;
  Constant *loopBeginFn;
  Constant *loopEndFn;
};

class FnVisitor : public InstVisitor<FnVisitor, void>
{
public:
  FnVisitor(LLVMContext &context);
  void visitModule(Module &module);
  void visitFunction(Function &fn);

private:
  LLVMContext &context;
  Constant *fnEnterFn;

  Id nextFnId;
};

class CallVisitor : public InstVisitor<CallVisitor, void>
{
public:
  CallVisitor(LLVMContext &context)
    : context(context), prevCall(NULL), prevInvoke(NULL)
  { }
  
  void visitModule(Module &module);
  void visitCallInst(CallInst &callInst);
  void visitInvokeInst(InvokeInst &invokeInst);
  void visitInstruction(Instruction &instruction);

private:
  LLVMContext &context;

  Constant* fnCallFn;
  Constant* fnRetFn;

  CallInst *prevCall;
  InvokeInst *prevInvoke;

  void addFnRetCall(InvokeInst *invoke, BasicBlock *dest);
};

}

#endif

