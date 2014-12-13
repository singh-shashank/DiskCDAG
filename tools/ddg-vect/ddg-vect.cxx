/* ddg-vect.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/Ids.hxx"
#include "LazyPartitionGraph.hxx"

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>

using namespace ddg;
using namespace llvm;
using namespace std;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::Required, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

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

  PassManager passes;
  passes.add(new Ids);
  passes.add(new LazyPartitionGraph);

  passes.run(*module.get());
}

