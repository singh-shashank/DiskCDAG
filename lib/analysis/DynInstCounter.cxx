#include "ddg/analysis/DynInstCounter.hxx"

namespace ddg
{

DynInstCounter::DynInstCounter(Ids& ids) : numInstrs(0), ids(ids)
{
}

int DynInstCounter::getCount()
{
	TraceTraversal traversal(ids);
	traversal.traverse(*this);
	return numInstrs;
}

void DynInstCounter::newTraceFile(string &fileName)
{
	numInstrs = 0;
}

void DynInstCounter::visitPHIs(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId, BasicBlock *prevBlock)
{
	for(BasicBlock::iterator it = begin; it != end; ++it)
		++numInstrs;
}

void DynInstCounter::visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId)
{
	for(BasicBlock::iterator it = begin; it != end; ++it)
	{
		if(!ids.isInstrumented(it))
			continue;
		if(it->isBinaryOp() || it->isCast() || isa<ReturnInst>(it) || isa<GetElementPtrInst>(it) || isa<CallInst>(it) || isa<InvokeInst>(it) || dyn_cast<InsertValueInst>(it) || dyn_cast<ExtractValueInst>(it))
		++numInstrs;
	}
}

void DynInstCounter::visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId)
{
	++numInstrs;
}

void DynInstCounter::visitStore(StoreInst *storeInst, Address addr, ExecutionId executionId)
{
	++numInstrs;
}

void DynInstCounter::visitReturn(Instruction *call, ExecutionId callExecutionId, ReturnInst *ret, ExecutionId retExecutionId)
{
	++numInstrs;
}

}

