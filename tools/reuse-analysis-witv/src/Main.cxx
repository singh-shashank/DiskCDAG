
// #include "GraphAnalysis/AnalyzeVectorOps.hxx"
// #include "GraphAnalysis/Graph.hxx"
// #include "GraphAnalysis/GraphTraversal.hxx"
// #include "GraphAnalysis/GraphNode.hxx"

// #include "GraphAnalysis/BuildTileGraph.hxx"
// #include "GraphAnalysis/ReadCDAG.hxx"
// #include "GraphAnalysis/CSRgraph.hxx"
// #include "GraphAnalysis/MacroDag.hxx"
// #include "GraphAnalysis/write_mem_trace.hxx"
// #include "GraphAnalysis/write_dinero_trace.hxx"
#include <fstream>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <math.h>
#include <sys/time.h>
#include <queue>
#include <iostream>
#include <tr1/unordered_map>

#include "ConvexPartitioning.hxx"

#define LEVEL1 0
#define MAX( X, Y) ( X > Y ? X : Y)
#define MIN( X, Y) ( X < Y ? X : Y)

//using namespace GraphAnalysis;
using namespace std;
using namespace std::tr1;

namespace opt = boost::program_options;

typedef vector<std::string>              StringVector;

bool VALID_CHECK = true;
bool ORIGINAL = false;
bool NEWSIBLING = false;
bool ALLNODES=false;
bool READCDAG=false;
bool DINERO_EN=false;
bool USE_METIS=false;
bool MULTI_LEVEL=false;

bool TRACE_PART = false;

static int Cs = 256/8;
static int HEURISTIC = 1;
static int NCOUNT = 1;		//default cnt = 1
static int TCOUNT = 1;		//default cnt = 1
static int CHILDCOUNT = 1;
static int loadStoreCount = 0;		// total load and store nodes
static bool  DEBUG_ON = false;
static int  BUILD_GRAPH = 0;
static int  JACOBI = 1;
static int pntOffset = 0;       //perpendicular, -1 for jacobi
bool TAKE_HALF=false;

static int MAX_IV_LEN = -1;                      // global value for maximum induction variable array length (2d+1)
static int MAX_DEPTH = -1;                      // global value for maximum induction variable array length (2d+1)
static int tileid = -1;         // SS- it was int32


static bool   g_verbose            = false;
static bool   g_script             = false;
static bool   g_printPartitions    = false;
static std::ofstream g_outStream;


double rtclock();

void printUsage(const opt::options_description& desc)
{
	std::cerr << desc << std::endl;
}

/************ MODIFIED TO PROCESS A SINGLE FILE AT A TIME *********************/

int main(int argc, char* argv[])
{

	double ts1, te1, ttotal;

	ts1 = rtclock();


	if(argc>2) TCOUNT = atoi(argv[2]);
	// //Cs *= 2;				// 2-M RCCP

	if(argc>3) NCOUNT = atoi(argv[3]);
	if(argc>4) CHILDCOUNT = atoi(argv[4]);

	#if 0
	int flag=0;
	if(argc>5) flag = atoi(argv[5]);
	if(flag) USE_METIS = true;

	flag = 0;
	if(argc>6) flag = atoi(argv[6]);
	if(flag) ORIGINAL = true;
	#endif
	// HEURISTIC = atoi(argv[3]);		else	   HEURISTIC = 1;

	opt::options_description optDesc("Options");
	optDesc.add_options()
		("metis,m", "Enable running with metis.")
		("original,o", "Generate original trace.")
		("level,l", "Enable multi-level partitioning.")
		("readcdag,r", "Read the cdag from the input file")
		("dinero,d", "Generate Trace in Dinero Format")
		("help,h",      "Show help/usage information.")
		("verbose,v",   "Enable verbose output.")
		("input-file",  opt::value<StringVector>(), "Set input file.")
		;

	opt::positional_options_description posDesc;
	posDesc.add("input-file", -1);

	opt::variables_map vars;
	opt::store(opt::command_line_parser(argc, argv).options(optDesc).positional(posDesc).run(), vars);
	opt::notify(vars);


	if(vars.count("metis"))
	{
		USE_METIS = true;
	}
	if(vars.count("original"))
	{
		ORIGINAL = true;
	}
	if(vars.count("dinero"))
	{
		DINERO_EN = true;
	}
	if(vars.count("level"))
	{
		MULTI_LEVEL = true;
	}
	if(vars.count("readcdag"))
	{
		READCDAG = true;
	}
	if(vars.count("verbose"))
	{
		DEBUG_ON = true;
		TRACE_PART = true;
	}
	if(vars.count("help"))
	{
		printUsage(optDesc);
		return 1;
	}
	// Main process trace
	if(vars.count("input-file"))
	{

		StringVector files = vars["input-file"].as<StringVector>();

		StringVector::iterator fileIter = files.begin();

		//for(fileIter = files.begin(); fileIter != files.end(); ++fileIter)
		//{
		//processTrace(*fileIter);
		//}
		//

		//for(fileIter = files.begin(); fileIter != files.end(); ++fileIter)
		{
			// ReadCDAG rcg;
			// rcg.init(files[0]);

			clock_t begin = clock();

			ConvexPartitioning cp;
			cp.init(files[0]);
			cp.generateConvexComponents(TCOUNT);
			cp.writeMemTraceForSchedule();
			cp.writeMemTraceForScheduleWithPool();

			clock_t end = clock();
    		double elapsed_time = double(end - begin) / CLOCKS_PER_SEC;

    		cout << "\n Time taken for ConvexPartitioning (Iteration vector heuristics) : " << elapsed_time / 60;
		}

	}
	else
	{
		std::cerr << "Fatal:  No input files specified." << std::endl;
		printUsage(optDesc);
		return 1;
	}

	if(g_outStream.is_open())
	{
		g_outStream.close();
	}

	if(g_verbose)
	{
		std::cerr << "[TILABILITY-ANALYSIS]  Summary:" << std::endl;
	}




	te1 = rtclock();
	ttotal = te1-ts1;
	//cout<< ttotal <<""<<endl;cout.flush();		//time in second
	cout << "\n";
	return 0;
}


double rtclock()
{
	struct timezone Tzp;
	struct timeval Tp;
	int stat;
	stat = gettimeofday (&Tp, &Tzp);
	if (stat != 0) printf("Error return from gettimeofday: %d",stat);
	return(Tp.tv_sec + Tp.tv_usec*1.0e-6);
}