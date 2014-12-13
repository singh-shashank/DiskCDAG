/* DebugStr.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef DEBUGSTR_HXX
#define DEBUGSTR_HXX

#include <llvm/Instruction.h>
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Support/raw_ostream.h>

namespace ddg
{

struct DebugStr
{
  DebugStr(llvm::Instruction *instruction) : instruction(instruction) { }

  llvm::Instruction* instruction;
};

llvm::raw_ostream& operator<<(llvm::raw_ostream &out, ddg::DebugStr d)
{
#if 0
  if (llvm::MDNode *dbg = d.instruction->getMetadata("dbg")) {
    llvm::DILocation loc(dbg);
    out << loc.getFilename() << "(" << loc.getLineNumber() << ")";
  }
#endif
  return out;
}

}

#endif

