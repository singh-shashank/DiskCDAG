#ifndef DYNINSTCOUNTER_HXX
#define DYNINSTCOUNTER_HXX

#include "ddg/analysis/Trace.hxx"

#include <llvm/Instructions.h>
#include <llvm/BasicBlock.h>

using namespace llvm;
using namespace std;

namespace ddg
{

class DynInstCounter : public TraceVisitor<DynInstCounter>
{
private:
	int numInstrs;
	Ids &ids;

public:
	DynInstCounter(Ids& ids);
	int getCount();

	void newTraceFile(string &fileName);
	void visitPHIs(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId, BasicBlock *prevBlock);
	void visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId);
	void visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId);
	void visitStore(StoreInst *storeInst, Address addr, ExecutionId executionId);
	void visitReturn(Instruction *call, ExecutionId callExecutionId, ReturnInst *ret, ExecutionId retExecutionId);
};

}
#endif
