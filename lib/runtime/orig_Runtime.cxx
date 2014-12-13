/* Runtime.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/common/types.hxx"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

using namespace std;

template <typename T>
static void write_as_chars(ostream &o, T *t)
{
  o.write(reinterpret_cast<const char*>(t), sizeof(T));
}

enum TraceType {
  FnCall=1,
  FnRet,
  FnEnter,
  BB,
  Load,
  Store,
  LoopBegin,
  LoopEnd,
  LoopIndVar32,
  LoopIndVar64,
  LoopEnter,
  LoopExit
};

extern "C"
{

static bool doTracing = false;

using namespace boost::iostreams;

static filtering_ostream trace;

static int nextTraceNumber = 1;

static double start;
static int tcount = 0;

#define CHECK_TRACING if (!doTracing) return;

void ddg_start_trace()
{
  if (doTracing) {
    return;
  }
  stringstream fname;
  fname << nextTraceNumber << ".trace";
  nextTraceNumber++;
  tcount++;
  trace.push(gzip_compressor() | file_sink(fname.str()));
  doTracing = true;
}

void ddg_start_trace_()
{
  ddg_start_trace();
}

void ddg_stop_trace()
{
  trace.reset();
  doTracing = false;
  if (tcount >= 3) {
    exit(0);
  }
}

void ddg_stop_trace_()
{
  ddg_stop_trace();
}

void ddg_load(Id dest, void* src)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(src);
  trace.put(Load);
  write_as_chars(trace, &dest);
  write_as_chars(trace, &addr);
}

void ddg_store(Id instrId, void* dest)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(dest);
  trace.put(Store);
  write_as_chars(trace, &instrId);
  write_as_chars(trace, &addr);
}

void ddg_function_call(Id instrId, void *address)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(address);
  trace.put(FnCall);
  write_as_chars(trace, &instrId);
  write_as_chars(trace, &addr);
}

void ddg_function_ret(Id instrId)
{
  CHECK_TRACING
  trace.put(FnRet);
  write_as_chars(trace, &instrId);
}

void ddg_function_enter(Id fnId, void *address)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(address);
  trace.put(FnEnter);
  write_as_chars(trace, &fnId);
  write_as_chars(trace, &addr);
}

void ddg_basic_block_enter(Id bbId)
{
  CHECK_TRACING
  trace.put(BB);
  write_as_chars(trace, &bbId);
}

void ddg_loop_begin(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopBegin);
  write_as_chars(trace, &loopId);
}

void ddg_loop_end(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopEnd);
  write_as_chars(trace, &loopId);
}

void ddg_loop_indvar32(Id instId, int32_t iv)
{
  CHECK_TRACING
  trace.put(LoopIndVar32);
  write_as_chars(trace, &instId);
  write_as_chars(trace, &iv);
}

void ddg_loop_indvar64(Id instId, int64_t iv)
{
  CHECK_TRACING
  trace.put(LoopIndVar64);
  write_as_chars(trace, &instId);
  write_as_chars(trace, &iv);
}

void ddg_loop_enter(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopEnter);
  write_as_chars(trace, &loopId);
}

void ddg_loop_exit(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopExit);
  write_as_chars(trace, &loopId);
}

void ddg_init()
{
}

void ddg_cleanup()
{
  if (doTracing) {
    ddg_stop_trace();
  }
}

}

