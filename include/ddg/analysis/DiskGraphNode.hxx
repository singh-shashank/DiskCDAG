#include <cassert>
#include <fstream>
#include <string>
#include <iostream>
#include <ostream>
#include <sstream>
#include <istream>
#include <deque>

using namespace std;

namespace ddg
{
struct GraphNode{
	Id dynId;	// Dynamic Id 
	Address addr;	// Addresses represented by this node if any
	unsigned int type;	// LLVM Type of the node
	Id staticId;
	std::deque<Id> predsList;	// Vector containing list of predecessors
	std::deque<Id> succsList;	// Vector containing list of successors

	// TODO : initialize it with proper values
	GraphNode(): dynId(0),
				addr(0),
				type(0),
				staticId(0)
	{
	}

	virtual void deepCopy(const GraphNode *node)
	{
		// deep copy this node
		dynId = node->dynId;
		addr = node->addr;
		type = node->type;
		staticId = node->staticId;
		for(int i=0; i < node->predsList.size(); ++i)
		{
			predsList.push_back(node->predsList[i]);
		}

		for(int i=0; i < node->succsList.size(); ++i)
		{
			succsList.push_back(node->succsList[i]);
		}
	}

	Id getId(){return dynId;}

	virtual void print(ostream &out) const
	{
		out << "\n" << dynId << ". ";
		out << "Instruction :" << llvm::Instruction::getOpcodeName(type) << " ";
		out << "StaticID : " << staticId << " ";
		out << "Address : " << addr << " ";
		out << " \n Num Predecessors: " << predsList.size() <<"\n";
		for(std::deque<Id>::const_iterator it = predsList.begin();
			it != predsList.end();
			++it)
		{
			out << *it << ",";
		}
		out << " \n Num Successor: " << succsList.size() <<"\n";
		for(std::deque<Id>::const_iterator it = succsList.begin();
			it != succsList.end();
			++it)
		{
			out << *it << ",";
		}
		out << "\n------------------------------------------------------\n";
	}

	// TODO : try taking in a output stream directly
	// 		  instead of returning a string
	virtual void writeToStream(stringstream &ss)
	{
		ss.str(std::string());				
		ss << dynId << " ";
		ss << staticId << " ";
		ss << type << " ";
		ss << addr << " ";
		ss << "\n";
		
		ss << predsList.size() << " ";
		for(std::deque<Id>::iterator it = predsList.begin();
			it != predsList.end();
			++it)
		{
			ss << *it << " ";
		}
		ss << "\n";

		ss << succsList.size() << " ";
		for(std::deque<Id>::iterator it = succsList.begin();
			it != succsList.end();
			++it)
		{
			ss << *it << " ";
		}
		ss << "\n";
	}

	virtual void writeToStreamInBinary(ostream &file)
	{
		file.write((const char*)&dynId, sizeof(Id));
		file.write((const char*)&staticId, sizeof(Id));
		file.write((const char*)&type, sizeof(unsigned int));
		file.write((const char*)&addr, sizeof(Address));

		Id predCount = predsList.size();
		file.write((const char*)&predCount, sizeof(Id));
		for(std::deque<Id>::iterator it = predsList.begin();
			it != predsList.end();
			++it)
		{
			Id temp = *it;
			file.write((const char*)&temp, sizeof(Id));
		}
		// copy(predsList.begin(), predsList.end(), ostream_iterator<Id>(file));

		Id succCount = succsList.size();
		file.write((const char*)&succCount, sizeof(Id));
		for(std::deque<Id>::iterator it = succsList.begin();
			it != succsList.end();
			++it)
		{
			Id temp = *it;
			file.write((const char*)&temp, sizeof(Id));
		}
		//copy(succsList.begin(), succsList.end(), ostream_iterator<Id>(file));
	}

	virtual void writeToStream(fstream &ss)
	{

	}

	virtual bool readNodeFromBinaryFile(istream &file)
	{
		file.read((char*)&dynId, sizeof(Id));
		file.read((char*)&staticId, sizeof(Id));
		file.read((char*)&type, sizeof(unsigned int));
		file.read((char*)&addr, sizeof(Address));

		Id predCount = 0;
		file.read((char*)&predCount, sizeof(Id));
		for(int i=0; i < predCount; ++i)
		{
			Id temp = 0;
			file.read((char*)&temp, sizeof(Id));
			predsList.push_back(temp);
			// TODO : how to make this cleaner implementation work?
			//file.read((char*)&predsList[i], sizeof(Id)); 
		}

		Id succCount = 0;
		file.read((char*)&succCount, sizeof(Id));
		for(int i=0; i < succCount; ++i)
		{
			Id temp = 0;
			file.read((char*)&temp, sizeof(Id));
			succsList.push_back(temp);
		}
		return false; // TODO : check for errors and return
	}

	virtual bool readNodeFromASCIIFile(istream &file)
	{
		
		

		// Read dynId, static Id, type and addr
		{
			std::string temp, line;
			std::stringstream tempss;
			getline(file, line);
			tempss << line;
			getline(tempss, temp, ' ');
			dynId = atoi(temp.c_str());
			getline(tempss, temp, ' ');
			staticId = atoi(temp.c_str());
			getline(tempss, temp, ' ');
			type = atoi(temp.c_str());
			getline(tempss, temp, ' ');
			addr = atol(temp.c_str());
		}

		// Read the predecessors
		{
			std::string temp, line;
			std::stringstream tempss;
			getline(file, line);
			tempss.str(string());
			tempss.str(line);
			getline(tempss, temp, ' '); // read the count
			while(getline(tempss, temp, ' ')) // start reading the preds
			{
				predsList.push_back(atoi(temp.c_str()));
			}
		}

		// Read the successors
		{
			std::string temp, line;
			std::stringstream tempss;
			getline(file, line);
			tempss.str(string());
			tempss.str(line);
			getline(tempss, temp, ' '); // read the count
			while(getline(tempss, temp, ' ')) // start reading the succs
			{
				succsList.push_back(atoi(temp.c_str()));
			}		
		}

		return false; // TODO compelete this. Return true if errors
	}

	virtual void reset()
	{
		dynId = 0;
		staticId = 0;
		addr = 0;
		type = 0;
		predsList.clear();
		succsList.clear();
	}

	virtual size_t getSize(unsigned int bs = 0)
	{
		// This is just a rough estimate - more accurately a lower bound.
		// But this wouldn't deviate a lot from actual size occupied
		// in memory.
		size_t retVal = 0;
		retVal += sizeof(Id) + sizeof(Address) + sizeof(Id) + sizeof(unsigned int);
		retVal += (predsList.size() + succsList.size())*sizeof(Id);
		retVal = retVal > sizeof(*this) ? retVal : sizeof(*this);
		if(bs != 0 && retVal >= bs)
		{
			cout << "\nError : Size of graph node with id : " << dynId;
			cout << " is " << retVal << " bytes, which is bigger then ";
			cout << " the specified block size of " << bs << " bytes";
			exit(0);
		}
		return retVal;
	}
};

struct LoopIV
{
	unsigned int loopId;
	int stmtNum;
	LoopIV(int id)
	{
		loopId = id;
		stmtNum = -1;
	}
};

struct GraphNodeWithIV : public GraphNode
{
	deque<unsigned int> loopItVec;
	GraphNodeWithIV(): GraphNode()
	{
	}

	virtual void deepCopy(const GraphNodeWithIV *node)
	{
		GraphNode::deepCopy(node);

		loopItVec.insert(loopItVec.begin(), node->loopItVec.begin(), node->loopItVec.end());
	}

	virtual void print(ostream &out) const
	{
		GraphNode::print(out);

		out << "\n Iteration Vector : " << loopItVec.size() << "\n";
		for(deque<unsigned int>::const_iterator it = loopItVec.begin(); 
			it != loopItVec.end(); ++it)
		{
			out << *it << " ";
		}
		out << "\n------------------------------------------------------\n";
	}

	virtual void writeToStream(stringstream &ss)
	{
		GraphNode::writeToStream(ss);

		ss << loopItVec.size() << " ";
		for(std::deque<unsigned int>::iterator it = loopItVec.begin();
			it != loopItVec.end();
			++it)
		{
			ss << *it << " ";
		}
		ss << "\n";
	}

	virtual void writeToStreamInBinary(ostream &file)
	{
		GraphNode::writeToStreamInBinary(file);

		unsigned int itVecCount = loopItVec.size();
		file.write((const char*)&itVecCount, sizeof(unsigned int));
		for(int i=0; i<itVecCount; ++i)
		{
			unsigned int temp = loopItVec[i];
			file.write((const char*)&temp, sizeof(unsigned int));
		}
	}

	virtual void writeToStream(fstream &ss)
	{
		GraphNode::writeToStream(ss);
	}

	virtual bool readNodeFromBinaryFile(istream &file)
	{
		GraphNode::readNodeFromBinaryFile(file);

		unsigned int itVecCount = 0;
		file.read((char*)&itVecCount, sizeof(unsigned int));
		for(int i=0; i < itVecCount; ++i)
		{
			unsigned int temp = 0;
			file.read((char*)&temp, sizeof(unsigned int));
			loopItVec.push_back(temp);
		}
		return false; // TODO : check for errors and return
	}

	virtual bool readNodeFromASCIIFile(istream &file)
	{
		GraphNode::readNodeFromASCIIFile(file);

		// Read the iteration vector
		{
			std::string temp, line;
			std::stringstream tempss;
			getline(file, line);
			tempss.str(string());
			tempss.str(line);
			getline(tempss, temp, ' '); // read the count
			while(getline(tempss, temp, ' ')) // start reading the iteration vector
			{
				loopItVec.push_back(atoi(temp.c_str()));
			}
		}
		return false; // TODO compelete this. Return true if errors	
	}

	virtual void reset()
	{
		GraphNode::reset();

		loopItVec.clear();
	}

	virtual size_t getSize(unsigned int bs = 0)
	{
		size_t retVal = 0;
		retVal += sizeof(Id) + sizeof(Address) + sizeof(Id) + sizeof(unsigned int);
		retVal += (predsList.size() + succsList.size())*sizeof(Id);
		retVal += (loopItVec.size())*sizeof(unsigned int);
		retVal = retVal > sizeof(*this) ? retVal : sizeof(*this);
		if(bs != 0 && retVal >= bs)
		{
			cout << "\nError : Size of graph node with id : " << dynId;
			cout << " is " << retVal << " bytes, which is bigger then ";
			cout << " the specified block size of " << bs << "bytes"; 
			exit(0);
		}
		return retVal;
	}
};

struct NodeNeighborInfo
{
	Id dynId;
	deque<Id> neighborList;

	NodeNeighborInfo() : dynId(0)
	{}

	Id getId(){return dynId;}

	virtual void print(ostream &out) const
	{
		out << "\n" << dynId << ". ";
		
		out << " \n Num Neighbors: " << neighborList.size() <<"\n";
		for(std::deque<Id>::const_iterator it = neighborList.begin();
			it != neighborList.end();
			++it)
		{
			out << *it << ",";
		}
		out << "\n------------------------------------------------------\n";
	}

	// TODO : try taking in a output stream directly
	// 		  instead of returning a string
	virtual void writeToStream(stringstream &ss)
	{
		ss.str(std::string());				
		ss << dynId << " ";

		ss << neighborList.size() << " ";
		for(std::deque<Id>::iterator it = neighborList.begin();
			it != neighborList.end();
			++it)
		{
			ss << *it << " ";
		}
		ss << "\n";
	}

	virtual void writeToStream(fstream &file)
	{
		// No need to implement unless you are planning to 
		// enable writeBack option for the cache
	}

	virtual void writeToStreamInBinary(ostream &file)
	{
		file.write((const char*)&dynId, sizeof(Id));
		
		Id neighborCount = neighborList.size();
		file.write((const char*)&neighborCount, sizeof(Id));
		for(std::deque<Id>::iterator it = neighborList.begin();
			it != neighborList.end();
			++it)
		{
			Id temp = *it;
			file.write((const char*)&temp, sizeof(Id));
		}
	}

	virtual bool readNodeFromBinaryFile(istream &file)
	{
		file.read((char*)&dynId, sizeof(Id));

		Id neighborCount = 0;
		file.read((char*)&neighborCount, sizeof(Id));
		for(int i=0; i < neighborCount; ++i)
		{
			Id temp = 0;
			file.read((char*)&temp, sizeof(Id));
			neighborList.push_back(temp);
		}
		return false; // TODO : check for errors and return
	}

	virtual void reset()
	{
		dynId = 0;
		neighborList.clear();
	}

	virtual size_t getSize(unsigned int bs = 0)
	{
		// This is just a rough estimate - more accurately a lower bound.
		// But this wouldn't deviate a lot from actual size occupied
		// in memory.
		size_t retVal = 0;
		retVal += sizeof(Id);
		retVal += (neighborList.size())*sizeof(Id);
		retVal = retVal > sizeof(*this) ? retVal : sizeof(*this);
		if(bs != 0 && retVal >= bs)
		{
			cout << "\nError : Size of neighbor node with id : " << dynId;
			cout << " is " << retVal << " bytes, which is bigger then ";
			cout << " the specified block size of " << bs << " bytes";
			exit(0);
		}
		return retVal;
	}
};
}