/* count-loops.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/Trace.hxx"

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>

using namespace ddg;
using namespace llvm;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::Required, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

class LoopCounter : public TraceVisitor<LoopCounter>
{
public:
  LoopCounter() : count(0), depth(0) {}

  void newTraceFile(string &fileName)
  {
    depth = 0;
  }

  void loopBegin(LoopBegin *loopBegin)
  {
    if (depth == 0) {
      ++count;
    }
    ++depth;
  }

  void loopEnd(LoopEnd *loopEnd)
  {
    --depth;
  }

  long getCount()
  {
    return count;
  }

private:
  long count;
  int depth;
};


int main(int argc, char **argv)
{
  cl::ParseCommandLineOptions(argc, argv, "DDG Analysis tool\n");

  llvm_shutdown_obj shutdownObj;  // Call llvm_shutdown() on exit.
  LLVMContext &context = getGlobalContext();

  SMDiagnostic err;

  auto_ptr<Module> module; 
  module.reset(ParseIRFile(InputFilename, err, context));

  if (module.get() == 0) {
    err.print(argv[0], errs());
    return 1;
  }

  Ids ids;
  ids.runOnModule(*module.get());

  LoopCounter loopCounter;

  TraceTraversal traversal(ids);
  traversal.traverse(loopCounter);

  errs() << "Top level loops: " << loopCounter.getCount() << "\n";
}

