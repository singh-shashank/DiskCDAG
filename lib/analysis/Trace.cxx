/* Trace.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/Trace.hxx"

namespace ddg
{

using namespace llvm;

cl::opt<string> traceFile("trace-file", cl::desc("Trace File"), cl::init(""));

}

