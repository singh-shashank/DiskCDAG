#ifndef CDAGCOUNTER_HXX
#define CDAGCOUNTER_HXX

#include "ddg/analysis/LazyGraph.hxx"

namespace ddg
{
using namespace llvm;
using namespace std;

class CDAGCounter : public TraceVisitor<CDAGCounter>
{
private:
	unsigned long count;
	Ids &ids;

public:
	CDAGCounter(Ids &ids) : ids(ids), count(0)
	{
	}

	unsigned long getCount()
	{
		TraceTraversal traversal(ids);
		traversal.traverse(*this);
		return count;
	}

	void newTraceFile(string &filename)
	{
		count = 0;
	}

	void visitBasicBlockRange(BasicBlock::iterator begin, BasicBlock::iterator end, ExecutionId executionId)
	{
		for(BasicBlock::iterator it = begin; it != end; ++it)
		{
			if(!ids.isInstrumented(it))
				continue;
#ifndef FULL_DAG
			if(it->isBinaryOp() && it->getType()->isFloatingPointTy())
#endif
				++count;
		}
	}

	void visitLoad(LoadInst *loadInst, Address addr, ExecutionId executionId)
	{
		++count;
#ifdef FULL_DAG
		++count; //one for the load node and one for the preceding input node
#endif
	}
};

}

#endif