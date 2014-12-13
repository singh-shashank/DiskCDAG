/* ddg-parallel.cxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#include "ddg/analysis/Ids.hxx"
#include "ddg/analysis/LazyGraph.hxx"
#include "ddg/analysis/DebugStr.hxx"

#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRReader.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>

#include <set>

using namespace ddg;
using namespace llvm;
using namespace std;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::Required, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));


raw_ostream& operator<<(raw_ostream &out, IterationVector &ivVec)
{
  out << "[";
  for (vector<IVEntry>::iterator it = ivVec.getIndvars().begin();
       it != ivVec.getIndvars().end();
       ++it) {
    out << " ";
    if (it->type == IVEntry::Loop) {
      out << "LOOP";
    }
    else {
      out << "CALLSITE";
    }
    out << "(" << it->id << ":" << it->value << ")";
  }
  return out << " ]";
}


struct UpdaterInfo
{
  bool isUpdater;
};


struct Violation
{
  enum Reason {
    LOAD_TO_NONUPDATE_STORE,
    STORE_TO_LOOP_CARRIED_READ,
    MULTIPLE_UPDATE_OPS,
    DEP_ON_ANOTHER_ITERATION
  };
  Reason reason;
  Instruction *from;
  Instruction *to;

  Violation(Reason reason, Instruction  *from, Instruction *to)
    : reason(reason), from(from), to(to) { }

  bool operator < (const Violation &other) const
  {
    if(this->from < other.from) {
      return true;
    }
    else if(this->from == other.from) {
      if(this->to < other.to) {
        return true;
      }
      else if(this->to == other.to) {
        return this->reason < other.reason;
      }
      else {
        return false;
      }
    }
    else {
      return false;
    }
  }

  void print(raw_ostream &out) const
  {
    switch(reason) {
      case LOAD_TO_NONUPDATE_STORE:
        out << "Load depends on a non update store ";
        break;
      case STORE_TO_LOOP_CARRIED_READ:
        out << "Store updating loop carried read ";
        break;
      case MULTIPLE_UPDATE_OPS:
        out << "Updates use different operators: ";
        break;
      case DEP_ON_ANOTHER_ITERATION:
        out << "Dependency on another iteration ";
        break;
    }
    out << DebugStr(from);
    if (reason == MULTIPLE_UPDATE_OPS) {
      out << " and ";
    }
    else {
      out << " --> ";
    }
    out << DebugStr(to) << "\n";

    /* out << *from << "\n" << *to << "\n\n"; */

  }
};


class DependencyVectorsCollector : public LazyGraphVisitor<UpdaterInfo>
{
public:
  DependencyVectorsCollector() { }

  virtual void visitNode(LazyNode<UpdaterInfo>::Handle node,
                         vector<LazyNode<UpdaterInfo>*> &predecessors,
                         vector<int> &operands) {
    if (!loopCarriedReads.size()) {
      return;
    }
    if (node->getInstruction()->getMetadata("canIv")) {
      return ;
    }

    if (LoadInst *load = dyn_cast<LoadInst>(node->getInstruction())) {
      Address addr = node->getAddress();

      bool hasViolatingStoreDependency = false;
      for (vector<LazyNode<UpdaterInfo>*>::iterator pred = predecessors.begin();
           pred != predecessors.end();
           ++pred) {
        LazyNode<UpdaterInfo>* predNode = *pred;

        if (isa<StoreInst>(predNode->getInstruction()) &&
            (predNode->get().isUpdater == false /*|| predNode->get().load != load*/)) {
          Id highestLoopId;
          if (node->getIterationVector()
                   .getDifferingLoop((*pred)->getIterationVector(), highestLoopId)) {
            hasViolatingStoreDependency = true;
  
            nonParallelLoops.insert(highestLoopId);

            Violation violation(Violation::LOAD_TO_NONUPDATE_STORE, node->getInstruction(),
                                predNode->getInstruction());
            loopViolations[highestLoopId].insert(violation);
          }
        }
      }

      if (currLoopReads.back().count(addr)) {
        currLoopReads.back().erase(addr);
        loopCarriedReads.back().erase(addr);
        loopCarriedReads.back().insert(make_pair(addr, node));
      }
      else if (!loopCarriedReads.back().count(addr)) {
        currLoopReads.back().insert(make_pair(addr, node));
      }

    }

    else if (StoreInst *store = dyn_cast<StoreInst>(node->getInstruction())) {
      Address addr = node->getAddress();

      node->get().isUpdater = false;
      if (!loopCarriedReads.back().count(addr) && currLoopReads.back().count(addr)) {
        LazyNode<UpdaterInfo>::Handle loadNode = currLoopReads.back()[addr];
        LoadInst *load = cast<LoadInst>(loadNode->getInstruction());
        Instruction *loadUse = cast<Instruction>(*load->use_begin());
               
        if (store->getValueOperand() == loadUse) {
          unsigned op = loadUse->getOpcode();

          if (currLoopUpdates.back().count(addr) && currLoopUpdates.back()[addr].op != op) {
            loopCarriedReads.back().erase(addr);
            loopCarriedReads.back().insert(make_pair(addr, loadNode));

            currLoopUpdates.back()[addr].store->get().isUpdater = false;
            currLoopUpdates.back().erase(addr);
          }
          else if (lastUpdaters.back().count(addr) && lastUpdaters.back()[addr].op != op) {
            loopCarriedReads.back().erase(addr);
            loopCarriedReads.back().insert(make_pair(addr, loadNode));

            LazyNode<UpdaterInfo>::Handle other = lastUpdaters.back()[addr].store;
            Id highestLoopId;
            if (node->getIterationVector().getHighestCommonLoop(
                    other->getIterationVector(), highestLoopId)) {
              nonParallelLoops.insert(highestLoopId);

              Violation violation(Violation::MULTIPLE_UPDATE_OPS, other->getInstruction(), store);
              loopViolations[highestLoopId].insert(violation);
            }
          }
          else {
            currLoopUpdates.back().erase(addr);
            currLoopReads.back().erase(addr);
            Updater updater = {node, op};
            currLoopUpdates.back().insert(make_pair(addr, updater));
            node->get().isUpdater = true;
          }
        }
      }

      int n = 0;
      vector<IVEntry>& indvars = node->getIterationVector().getIndvars();
      if (indvars.size()) {
        for (vector<AddrMap>::iterator it = loopCarriedReads.begin();
             it != loopCarriedReads.end(); ++it, ++n) {
          while (indvars[n].type == IVEntry::CallSite) {
            ++n;
          }
          if (it->count(addr)) {
            nonParallelLoops.insert(indvars[n].id);

            Violation violation(Violation::STORE_TO_LOOP_CARRIED_READ, node->getInstruction(),
                                (*it)[addr]->getInstruction());
            loopViolations[indvars[n].id].insert(violation);
          }
        }
      }
    }

    else {
      IterationVector &ivVec = node->getIterationVector();
      for (vector<LazyNode<UpdaterInfo>*>::iterator pred = predecessors.begin();
           pred != predecessors.end();
           ++pred) {
        if ((*pred)->getInstruction()->getMetadata("canIv")) {
          continue;
        }

        Instruction::op_iterator oBegin = (*pred)->getInstruction()->op_begin(),
                                 oEnd   = (*pred)->getInstruction()->op_end();
        if (find(oBegin, oEnd, node->getInstruction()) != oEnd) {
          continue;
        }

        Id highestLoopId;
        if (ivVec.getDifferingLoop((*pred)->getIterationVector(), highestLoopId)) {
          nonParallelLoops.insert(highestLoopId);

          Violation violation(Violation::DEP_ON_ANOTHER_ITERATION, node->getInstruction(),
                              (*pred)->getInstruction());
          loopViolations[highestLoopId].insert(violation);
        }
      }
    }
  }
  
  virtual void visitLoopIterEnd()
  {
    loopCarriedReads.back().insert(currLoopReads.back().begin(), currLoopReads.back().end());
    currLoopReads.back().clear();

    for (DenseMap<Address, Updater>::iterator it = currLoopUpdates.back().begin();
         it != currLoopUpdates.back().end(); ++it) {
      lastUpdaters.back()[it->first] = it->second;
    }
    currLoopUpdates.back().clear();
  }

  virtual void visitLoopBegin(int loopId)
  {
    loopCarriedReads.push_back(AddrMap());
    currLoopReads.push_back(AddrMap());

    lastUpdaters.push_back(DenseMap<Address, Updater>());
    currLoopUpdates.push_back(DenseMap<Address, Updater>());

    allLoops.insert(loopId);
  }

  virtual void visitLoopEnd(int loopId)
  {
    currLoopReads.pop_back();
    if (loopCarriedReads.size() >= 2) {
      for (AddrMap::iterator it = loopCarriedReads.back().begin();
           it != loopCarriedReads.back().end(); ++it) {
        currLoopReads.back()[it->first] = it->second;
      }
    }
    loopCarriedReads.pop_back();

    if (!nonParallelLoops.count(loopId) && lastUpdaters.back().size()) {
      reducibleLoops.insert(loopId);
    }
    for (DenseMap<Address, Updater>::iterator it = lastUpdaters.back().begin();
         it != lastUpdaters.back().end(); ++it) {
      loopReductions[loopId].insert(it->second.store->getInstruction());
    }
    if (lastUpdaters.size() >= 2) {
      DenseMap<Address, Updater>& prev = lastUpdaters[lastUpdaters.size()-2];
      DenseMap<Address, Updater>& curr = lastUpdaters.back();
      for (DenseMap<Address, Updater>::iterator it = curr.begin();
           it != curr.end(); ++it) {
        prev[it->first] = it->second;
      }
    }
    lastUpdaters.pop_back();
    currLoopUpdates.pop_back();
  }
  
private:
  typedef DenseMap<Address, LazyNode<UpdaterInfo>::Handle> AddrMap;

  vector<AddrMap> currLoopReads;
  vector<AddrMap> loopCarriedReads;

  struct Updater
  {
    LazyNode<UpdaterInfo>::Handle store;
    unsigned op;
  };
  vector<DenseMap<Address, Updater> > currLoopUpdates;
  vector<DenseMap<Address, Updater> > lastUpdaters;

public:
  DenseSet<Id> allLoops;

  DenseSet<Id> reducibleLoops;
  DenseMap<Id, DenseSet<Instruction*> > loopReductions;

  DenseSet<Id> nonParallelLoops;
  DenseMap<Id, set<Violation> > loopViolations;
};

class ParallelLoops : public ModulePass
{
public:
  static char ID;

  ParallelLoops() : ModulePass(ID) {}

  bool runOnModule(Module &module)
  {
    Ids& ids = getAnalysis<Ids>();

    DependencyVectorsCollector dep;
    dep.visit(ids);

    set<Id> parallelLoops;
    for (DenseSet<Id>::iterator it = dep.allLoops.begin();
         it != dep.allLoops.end(); ++it) {
      if (dep.nonParallelLoops.count(*it) || dep.reducibleLoops.count(*it)) continue;
      parallelLoops.insert(*it);
    }
    for (set<Id>::iterator it = parallelLoops.begin(); it != parallelLoops.end(); ++it) {
      errs() << "Parallel loop: " << *it << " @ "
             << DebugStr(ids.getLoop(*it)->getFirstInsertionPt());
      errs() << "\n\n";
    }

    set<Id> reducibleLoops;
    for (DenseSet<Id>::iterator it = dep.reducibleLoops.begin();
         it != dep.reducibleLoops.end(); ++it) {
      if (dep.nonParallelLoops.count(*it)) continue;
      reducibleLoops.insert(*it);
    }
    for (set<Id>::iterator it = reducibleLoops.begin(); it != reducibleLoops.end(); ++it) {
      errs() << "Reducible loop: " << *it << " @ "
             << DebugStr(ids.getLoop(*it)->getFirstInsertionPt())
             << "\n------------------------------------\n";
      for (DenseSet<Instruction*>::iterator instIt = dep.loopReductions[*it].begin();
           instIt != dep.loopReductions[*it].end(); ++instIt) {
        errs() << "Reduction at " << DebugStr(*instIt) << "\n";
      }
      errs() << "\n";
    }

    set<Id> nonParallelLoops(dep.nonParallelLoops.begin(), dep.nonParallelLoops.end());
    for (set<Id>::iterator it = nonParallelLoops.begin(); it != nonParallelLoops.end(); ++it) {
      errs() << "Non parallel loop: " << *it << " @ "
             << DebugStr(ids.getLoop(*it)->getFirstInsertionPt())
             << "\n------------------------------------\n";
      for (set<Violation>::iterator rit = dep.loopViolations[*it].begin(),
                                 rend = dep.loopViolations[*it].end();
           rit != rend; ++rit) {
        rit->print(errs());
      }
      errs() << "\n";
    }

    return false;
  }

  void getAnalysisUsage(AnalysisUsage &usage) const
  {
    usage.setPreservesAll();
    usage.addRequiredTransitive<Ids>();
  }
};

char ParallelLoops::ID = 0;

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
  passes.add(new ParallelLoops);

  passes.run(*module.get());
}

