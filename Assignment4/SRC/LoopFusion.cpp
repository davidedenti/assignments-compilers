//===----------------------------------------------------------------------===//
// Assignment 4 - Loop Fusion
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

namespace {

void collectInnermostLoops(Loop *L, std::vector<Loop *> &Worklist) {
  if (L->isInnermost()) {
    Worklist.push_back(L);
    return;
  }

  for (Loop *SottoLoop : L->getSubLoops()) {
    collectInnermostLoops(SottoLoop, Worklist);
  }
}

BasicBlock *getNonLoopSuccessor(Loop &L) {
  BranchInst *GuardBranch = L.getLoopGuardBranch();

  if (!GuardBranch) { return nullptr; }

  BasicBlock *Preheader = L.getLoopPreheader();

  if (!Preheader) { return nullptr; }

  BasicBlock *Successore0 = GuardBranch->getSuccessor(0);
  BasicBlock *Successore1 = GuardBranch->getSuccessor(1);

  if (Successore0 == Preheader) { return Successore1; }
  if (Successore1 == Preheader) { return Successore0; }

  return nullptr;
}

bool areAdjacent(Loop &L0, Loop &L1) {
  BranchInst *GuardBranchL0 = L0.getLoopGuardBranch();

  if (GuardBranchL0) {

    BasicBlock *NonLoopSuccessorL0 = getNonLoopSuccessor(L0);

    if (!NonLoopSuccessorL0) { 
        return false; 
    }

    BranchInst *GuardBranchL1 = L1.getLoopGuardBranch();

    if (!GuardBranchL1) 
        return false;

    BasicBlock *EntryL1 = GuardBranchL1->getParent();

    return NonLoopSuccessorL0 == EntryL1;

  } else {

    BasicBlock *ExitL0 = L0.getExitBlock();
    BasicBlock *PreheaderL1 = L1.getLoopPreheader();

    if (!ExitL0 || !PreheaderL1)
        return false;

    if(ExitL0 == PreheaderL1) return true;

    if (ExitL0->size() != 1)
      return false;

    auto *ExitBranch = dyn_cast<BranchInst>(ExitL0->getTerminator());
    return ExitBranch && ExitBranch->isUnconditional() &&
           ExitBranch->getSuccessor(0) == PreheaderL1;

  }

}

bool haveSameTripCount(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  const SCEV *TripCountL0 = SE.getBackedgeTakenCount(&L0);
  const SCEV *TripCountL1 = SE.getBackedgeTakenCount(&L1);

  if (isa<SCEVCouldNotCompute>(TripCountL0)) { return false; }
  if (isa<SCEVCouldNotCompute>(TripCountL1)) { return false; }

  return TripCountL0 == TripCountL1;
}


BasicBlock *getEntryBlock(Loop &L) {
  BranchInst *GuardBranch = L.getLoopGuardBranch();

  if (GuardBranch) { return GuardBranch->getParent(); }

  return L.getLoopPreheader();
}


bool areControlFlowEquivalent(Loop &L0, Loop &L1, DominatorTree &DT, PostDominatorTree &PDT) {
  BasicBlock *EntryL0 = getEntryBlock(L0);
  BasicBlock *EntryL1 = getEntryBlock(L1);

  if (!EntryL0 || !EntryL1) { return false; }

  bool L0DominaL1 = DT.dominates(EntryL0, EntryL1);
  bool L1PostDominaL0 = PDT.dominates(EntryL1, EntryL0);

  return L0DominaL1 && L1PostDominaL0;
}

bool hasNegativeDistanceDependence(Loop &L0, Loop &L1, DependenceInfo &DI) {
  for (BasicBlock *BB0 : L0.blocks()) {
    for (Instruction &I0 : *BB0) {
      for (BasicBlock *BB1 : L1.blocks()) {
        for (Instruction &I1 : *BB1) {
          // Per una dipendenza tra L0 e L1, una direzione GT al livello del
          // loop significa che L1 deve usare un'iterazione precedente a
          // quella di L0. L'ordine verrebbe invertito dalla fusione.
          if (!I0.mayWriteToMemory() || !I1.mayReadOrWriteMemory()) {
            continue;
          }

          std::unique_ptr<Dependence> Dipendenza = DI.depends(&I0, &I1, true);
          if (!Dipendenza) {
            continue;
          }

          unsigned Livello = L0.getLoopDepth();
          if (Dipendenza->isConfused() || Dipendenza->getLevels() < Livello) {
            outs() << "Dipendenza non determinabile\n";
            return true;
          }

          unsigned Direzione = Dipendenza->getDirection(Livello);
          if (Direzione & Dependence::DVEntry::GT) {
            outs() << "Dipendenza backward\n";
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool canFuse(Loop &L0, Loop &L1, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE, DependenceInfo &DI) {
  if (L0.getParentLoop() != L1.getParentLoop()) { return false; }
  if (!areAdjacent(L0, L1)) { return false; }
  if (!haveSameTripCount(L0, L1, SE)) { return false; }
  if (!areControlFlowEquivalent(L0, L1, DT, PDT)) { return false; }
  if (hasNegativeDistanceDependence(L0, L1, DI)) { return false; }

  return true;
}

bool fuseLoops(Loop &L0, Loop &L1) {
  // TODO:
  // 1. recuperare le induction variables di L0 e L1;
  // 2. sostituire gli usi dell'IV di L1 con l'IV di L0;
  // 3. modificare i branch per collegare body L0 -> body L1;
  // 4. usare il latch e l'uscita di L1 per il loop fuso.
  (void)L0;
  (void)L1;

  return false;
}

struct LoopFusionPass : PassInfoMixin<LoopFusionPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
    PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
    ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    DependenceInfo &DI = AM.getResult<DependenceAnalysis>(F);

    std::vector<Loop *> Worklist;
    bool IRModificato = false;

    for (Loop *TopLevelLoop : llvm::reverse(LI)) {
      collectInnermostLoops(TopLevelLoop, Worklist);
    }

    for (size_t Indice = 0; Indice + 1 < Worklist.size(); ++Indice) {
      Loop *L0 = Worklist[Indice];
      Loop *L1 = Worklist[Indice + 1];

      outs() << "Analizzo possibile fusione tra: ";
      outs() << L0->getHeader()->getName() << " e ";
      outs() << L1->getHeader()->getName() << "\n";

      if (!canFuse(*L0, *L1, DT, PDT, SE, DI)) {
        outs() << "I loop non possono essere fusi\n";
        continue;
      }

      outs() << "I loop possono essere fusi\n";

/*       if (fuseLoops(*L0, *L1)) {
        IRModificato = true;
        break;
      } */
    }

    if (IRModificato) { return PreservedAnalyses::none(); }

    return PreservedAnalyses::all();
  }
};

} // namespace

PassPluginLibraryInfo getLocalOptsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "loop-fusion-pass") {
                    FPM.addPass(LoopFusionPass());
                    return true;
                  }

                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getLocalOptsPluginInfo();
}
