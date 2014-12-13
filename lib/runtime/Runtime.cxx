#include "ddg/common/types.hxx"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <set>

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;

static std::ofstream outfile("trace.txt");
const char linend = '\n';
string idS = "  id: ";
string addrS = "  addr: ";
string traceTypeS = "  traceType: ";

template <typename T>
static void write_as_chars(ostream &o, T *t)
{
	o.write(reinterpret_cast<const char*>(t), sizeof(T));
}

template <typename T>
static void write_trace_as_txt(T *t)
{
	std::stringstream temp;
	temp << (*t);
	if (outfile.is_open())
	{
		outfile << temp.str();
	}
	else
	{
		std::cout << "\n trace.txt not open";
	}
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
  LoopExit,
  RegionBegin,
  RegionEnd
};

static set<int> sampleNums;

extern "C"
{

static bool doTracing = false;

using namespace boost::iostreams;
using namespace boost;

static filtering_ostream trace;

static int nextTraceNumber = 1;
static int traceId = 0;

static double start;
static int tcount = 0;

#define CHECK_TRACING if (!doTracing) return;

void ddg_start_trace()
{
  if (doTracing) {
    return;
  }

  ++traceId;
  if (!sampleNums.count(traceId)) {
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

  if (traceId == *sampleNums.rbegin()) {
    exit(0);
  }
}

void ddg_stop_trace_()
{
  ddg_stop_trace();
}

void ddg_region_begin()
{
  CHECK_TRACING
  trace.put(RegionBegin);
  Id instrId= 1089;
  write_as_chars(trace, &instrId);

  string type = "ddg_region_begin";
  write_trace_as_txt(&type);
  write_trace_as_txt(&linend);
}

void ddg_region_end()
{
  CHECK_TRACING
  trace.put(RegionEnd);
  Id instrId= 1090;
  write_as_chars(trace, &instrId);

  string type = "ddg_region_end";
  write_trace_as_txt(&type);
  write_trace_as_txt(&linend);
}

void ddg_load(Id dest, void* src)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(src);
  trace.put(Load);
  write_as_chars(trace, &dest);
  write_as_chars(trace, &addr);

  string type = "Load";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&dest);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&addr);
  write_trace_as_txt(&linend);
}

void ddg_store(Id instrId, void* dest)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(dest);
  trace.put(Store);
  write_as_chars(trace, &instrId);
  write_as_chars(trace, &addr);

  string type = "Store";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&instrId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&addr);
  write_trace_as_txt(&linend);
}

void ddg_function_call(Id instrId, void *address)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(address);
  trace.put(FnCall);
  write_as_chars(trace, &instrId);
  write_as_chars(trace, &addr);
  
  string type = "FnCall";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&instrId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&addr);
  write_trace_as_txt(&linend);
}

void ddg_function_ret(Id instrId)
{
  CHECK_TRACING
  trace.put(FnRet);
  write_as_chars(trace, &instrId);
  
  string type = "FnReturn";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&instrId);
  write_trace_as_txt(&linend);
}

void ddg_function_enter(Id fnId, void *address)
{
  CHECK_TRACING
  Address addr = reinterpret_cast<Address>(address);
  trace.put(FnEnter);
  write_as_chars(trace, &fnId);
  write_as_chars(trace, &addr);

  string type = "FnEnter";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&fnId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&addr);
  write_trace_as_txt(&linend);
}

void ddg_basic_block_enter(Id bbId)
{
  CHECK_TRACING
  trace.put(BB);
  write_as_chars(trace, &bbId);

  string type = "BB";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&bbId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&linend);
}

void ddg_loop_begin(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopBegin);
  write_as_chars(trace, &loopId);

  string type = "LoopBegin";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&loopId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&linend);
}

void ddg_loop_end(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopEnd);
  write_as_chars(trace, &loopId);

  string type = "LoopEnd";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&loopId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&linend);
}

void ddg_loop_indvar32(Id instId, int32_t iv)
{
  CHECK_TRACING
  trace.put(LoopIndVar32);
  write_as_chars(trace, &instId);
  write_as_chars(trace, &iv);

  string type = "Loop Indvar32";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&instId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&iv);
  write_trace_as_txt(&linend);
}

void ddg_loop_indvar64(Id instId, boost::int64_t iv)
{
  CHECK_TRACING
  trace.put(LoopIndVar64);
  write_as_chars(trace, &instId);
  write_as_chars(trace, &iv);

  string type = "Loop indvar64";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&instId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&iv);
  write_trace_as_txt(&linend);
}

void ddg_loop_enter(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopEnter);
  write_as_chars(trace, &loopId);

  string type = "LoopEnter";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&loopId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&linend);
}

void ddg_loop_exit(Id loopId)
{
  CHECK_TRACING
  trace.put(LoopExit);
  write_as_chars(trace, &loopId);

  string type = "LoopExit";
  write_trace_as_txt(&traceTypeS);
  write_trace_as_txt(&type);
  write_trace_as_txt(&idS);
  write_trace_as_txt(&loopId);
  write_trace_as_txt(&addrS);
  write_trace_as_txt(&linend);
}

void ddg_init()
{
  char *env = getenv("DDG_SAMPLE_NUMS");
  if (!env) {
    sampleNums.insert(1);
    sampleNums.insert(2);
    sampleNums.insert(3);
  }
  else {
    string s = env;
    tokenizer<> tok(s);

    for (tokenizer<>::iterator it = tok.begin(); it != tok.end(); ++it) {
      sampleNums.insert(lexical_cast<int>(*it));
    }
  }
}

void ddg_cleanup()
{
  if (doTracing) {
    ddg_stop_trace();
  }
}

}

