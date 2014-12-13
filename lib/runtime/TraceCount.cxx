/* TraceCount.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/common/types.hxx"

#include <fstream>

using namespace std;

extern "C"
{

static long traceCount = 0;

void ddg_start_trace()
{
  ++traceCount;
}

void ddg_start_trace_()
{
  ddg_start_trace();
}

void ddg_stop_trace()
{
}

void ddg_stop_trace_()
{
  ddg_stop_trace();
}

void ddg_load(Id dest, void* src)
{
}

void ddg_store(Id instrId, void* dest)
{
}

void ddg_function_call(Id instrId, void *address)
{
}

void ddg_function_ret(Id instrId)
{
}

void ddg_function_enter(Id fnId, void *address)
{
}

void ddg_basic_block_enter(Id bbId)
{
}

void ddg_loop_begin(Id loopId)
{
}

void ddg_loop_end(Id loopId)
{
}

void ddg_loop_indvar32(Id instId, int32_t iv)
{
}

void ddg_loop_indvar64(Id instId, int64_t iv)
{
}

void ddg_loop_enter(Id loopId)
{
}

void ddg_loop_exit(Id loopId)
{
}

void ddg_init()
{
}

void ddg_cleanup()
{
  ofstream out("trace.count");
  out << traceCount << "\n";
  out.close();
}

}

