#include <map>
#include <set>
#include <climits>
#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <typeinfo>


class DiskCDAG;
class DiskBlockManager
{

public:
	DiskBlockManager(size_t bs):blockSize(bs),
								curBlockNum(0),
                                curBlockSize(0)
    {

    }

    size_t getCurBlockNum()
    {
        return curBlockNum;
    }

private:
	size_t blockSize;
	size_t curBlockNum;
    size_t curBlockSize;
    DiskCDAG *cdag;

};