//===----------------------------------------------------------------------===//
// Assignment 4 - Loop Fusion
//
// Gestisce sia loop non guarded (top-tested) sia loop guarded (ruotati).
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
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

// Recupera l'induction variable in modo robusto per entrambe le forme di loop:
// - loop guarded/ruotati: getInductionVariable(SE) (confronto nel latch);
// - loop non guarded/top-tested: getInductionVariable e' nullo, si ripiega su
//   getCanonicalInductionVariable() (IV canonica {0,+,1} nell'header).
PHINode *getInduction(Loop &L, ScalarEvolution &SE) {
  if (PHINode *IV = L.getInductionVariable(SE)) { return IV; }

  return L.getCanonicalInductionVariable();
}

BasicBlock *getNonLoopSuccessor(Loop &L) {
  BranchInst *GuardBranch = L.getLoopGuardBranch();

  if (!GuardBranch || !GuardBranch->isConditional()) { return nullptr; }

  BasicBlock *Preheader = L.getLoopPreheader();

  if (!Preheader) { return nullptr; }

  BasicBlock *Successore0 = GuardBranch->getSuccessor(0);
  BasicBlock *Successore1 = GuardBranch->getSuccessor(1);

  if (Successore0 == Preheader) { return Successore1; }
  if (Successore1 == Preheader) { return Successore0; }

  return nullptr;
}

BasicBlock *getEntryBlock(Loop &L) {
  BranchInst *GuardBranch = L.getLoopGuardBranch();

  if (GuardBranch) { return GuardBranch->getParent(); }

  return L.getLoopPreheader();
}

bool haveSameGuard(Loop &L0, Loop &L1) {
  BranchInst *GuardL0 = L0.getLoopGuardBranch();
  BranchInst *GuardL1 = L1.getLoopGuardBranch();

  if (!GuardL0 || !GuardL1 || !GuardL0->isConditional() || !GuardL1->isConditional()) { return false; }
  if (GuardL0->getCondition() != GuardL1->getCondition()) { return false; }

  BasicBlock *PreheaderL0 = L0.getLoopPreheader();
  BasicBlock *PreheaderL1 = L1.getLoopPreheader();

  if (!PreheaderL0 || !PreheaderL1) { return false; }

  bool PreheaderTrovatoL0 = GuardL0->getSuccessor(0) == PreheaderL0 || GuardL0->getSuccessor(1) == PreheaderL0;
  bool PreheaderTrovatoL1 = GuardL1->getSuccessor(0) == PreheaderL1 || GuardL1->getSuccessor(1) == PreheaderL1;

  if (!PreheaderTrovatoL0 || !PreheaderTrovatoL1) { return false; }

  bool LoopSuSuccessoreZeroL0 = GuardL0->getSuccessor(0) == PreheaderL0;
  bool LoopSuSuccessoreZeroL1 = GuardL1->getSuccessor(0) == PreheaderL1;

  return LoopSuSuccessoreZeroL0 == LoopSuSuccessoreZeroL1;
}

//===----------------------------------------------------------------------===//
// Verifica delle condizioni di fusione
//===----------------------------------------------------------------------===//

// 1) Adiacenza. Per loop guarded il successore non-loop della guardia di L0 deve
//    essere l'entry di L1; per loop non guarded l'exit block di L0 deve
//    coincidere con il preheader di L1.
bool areAdjacent(Loop &L0, Loop &L1) {
  BranchInst *GuardL0 = L0.getLoopGuardBranch();
  BranchInst *GuardL1 = L1.getLoopGuardBranch();

  if ((GuardL0 && !GuardL1) || (!GuardL0 && GuardL1)) { return false; }

  if (GuardL0 && GuardL1) {
    BasicBlock *NonLoopSuccessorL0 = getNonLoopSuccessor(L0);
    BasicBlock *EntryL1 = GuardL1->getParent();

    if (!NonLoopSuccessorL0 || !EntryL1) { return false; }

    return NonLoopSuccessorL0 == EntryL1 && L0.getExitBlock() == EntryL1;
  }

  BasicBlock *ExitL0 = L0.getExitBlock();
  BasicBlock *PreheaderL1 = L1.getLoopPreheader();

  if (!ExitL0 || !PreheaderL1) { return false; }
  if (ExitL0 != PreheaderL1) { return false; }

  // Il blocco che collega i due loop deve contenere solo il branch: se c'e'
  // un'altra istruzione (es. uno store) allora tra i loop viene eseguito del
  // codice e i loop NON sono adiacenti.
  for (Instruction &I : *ExitL0) {
    if (!isa<PHINode>(&I) && !I.isTerminator()) { return false; }
  }

  return true;
}

// 2) Stesso trip count, tramite ScalarEvolution.
bool haveSameTripCount(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  const SCEV *TripCountL0 = SE.getBackedgeTakenCount(&L0);
  const SCEV *TripCountL1 = SE.getBackedgeTakenCount(&L1);

  if (isa<SCEVCouldNotCompute>(TripCountL0)) { return false; }
  if (isa<SCEVCouldNotCompute>(TripCountL1)) { return false; }

  return TripCountL0 == TripCountL1;
}

// 3) Control-flow equivalence: L0 domina L1 e L1 post-domina L0.
bool areControlFlowEquivalent(Loop &L0, Loop &L1, DominatorTree &DT, PostDominatorTree &PDT) {
  BasicBlock *EntryL0 = getEntryBlock(L0);
  BasicBlock *EntryL1 = getEntryBlock(L1);

  if (!EntryL0 || !EntryL1) { return false; }

  bool L0DominaL1 = DT.dominates(EntryL0, EntryL1);
  bool L1PostDominaL0 = PDT.dominates(EntryL1, EntryL0);

  return L0DominaL1 && L1PostDominaL0;
}

// 4) Dipendenze a distanza negativa, via SCEV (indipendente dalla forma del
//    loop). Per ogni scrittura di L0 e ogni accesso di L1 allo STESSO array, si
//    confronta l'indirizzo di partenza (iterazione 0) dei rispettivi
//    add-recurrence. Se il secondo loop accede a un elemento con offset dello
//    stesso segno del passo (un elemento che il primo loop produce in
//    un'iterazione futura), la fusione invertirebbe l'ordine -> vietata.
//
//    Due alloca distinti non aliasano, quindi accessi ad array diversi sono
//    indipendenti; con puntatori che potrebbero aliasare questo controllo
//    sarebbe insufficiente.
bool hasNegativeDistanceDependence(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  SmallVector<Instruction *, 8> ScrittureL0;
  SmallVector<Instruction *, 8> AccessiL1;

  for (BasicBlock *BB : L0.blocks()) {
    for (Instruction &I : *BB) {
      if (isa<StoreInst>(&I)) { ScrittureL0.push_back(&I); }
    }
  }

  for (BasicBlock *BB : L1.blocks()) {
    for (Instruction &I : *BB) {
      if (getLoadStorePointerOperand(&I)) { AccessiL1.push_back(&I); }
    }
  }

  for (Instruction *Scrittura : ScrittureL0) {
    Value *PtrScrittura = getLoadStorePointerOperand(Scrittura);
    const SCEV *ScevScrittura = SE.getSCEV(PtrScrittura);
    const auto *ArScrittura = dyn_cast<SCEVAddRecExpr>(ScevScrittura);

    if (!ArScrittura || !ArScrittura->isAffine()) {
      outs() << "Dipendenza non determinabile\n";
      return true;
    }

    for (Instruction *Accesso : AccessiL1) {
      Value *PtrAccesso = getLoadStorePointerOperand(Accesso);

      // Array diversi (alloca distinti) -> nessuna dipendenza.
      if (getUnderlyingObject(PtrScrittura) != getUnderlyingObject(PtrAccesso)) {
        continue;
      }

      const SCEV *ScevAccesso = SE.getSCEV(PtrAccesso);
      const auto *ArAccesso = dyn_cast<SCEVAddRecExpr>(ScevAccesso);

      if (!ArAccesso || !ArAccesso->isAffine()) {
        outs() << "Dipendenza non determinabile\n";
        return true;
      }

      const SCEV *PassoScrittura = ArScrittura->getStepRecurrence(SE);
      const SCEV *PassoAccesso = ArAccesso->getStepRecurrence(SE);

      if (PassoScrittura != PassoAccesso) {
        outs() << "Dipendenza non determinabile\n";
        return true;
      }

      // Delta = start(L1) - start(L0). Prodotto col passo > 0 significa stesso
      // segno: L1 accede in avanti rispetto a cio' che L0 scrive.
      const SCEV *Delta = SE.getMinusSCEV(ArAccesso->getStart(), ArScrittura->getStart());
      const SCEV *Prodotto = SE.getMulExpr(Delta, PassoScrittura);

      if (SE.isKnownPositive(Prodotto)) {
        outs() << "Dipendenza backward\n";
        return true;
      }

      if (SE.isKnownNonPositive(Prodotto)) {
        continue; // distanza 0 o all'indietro: sicuro
      }

      outs() << "Dipendenza non determinabile\n";
      return true;
    }
  }

  return false;
}

bool canFuse(Loop &L0, Loop &L1, DominatorTree &DT, PostDominatorTree &PDT, ScalarEvolution &SE) {
  if (L0.getParentLoop() != L1.getParentLoop()) { return false; }
  if (!areAdjacent(L0, L1)) { return false; }
  if (L0.getLoopGuardBranch() && !haveSameGuard(L0, L1)) { return false; }
  if (!haveSameTripCount(L0, L1, SE)) { return false; }
  if (!areControlFlowEquivalent(L0, L1, DT, PDT)) { return false; }
  if (hasNegativeDistanceDependence(L0, L1, SE)) { return false; }

  return true;
}

//===----------------------------------------------------------------------===//
// Trasformazione del codice
//===----------------------------------------------------------------------===//

void replaceInductionVariableUses(Loop &L1, PHINode *InductionL0, PHINode *InductionL1) {
  BasicBlock *HeaderL1 = L1.getHeader();
  BasicBlock *LatchL1 = L1.getLoopLatch();

  for (BasicBlock *BB : L1.blocks()) {
    if (BB == HeaderL1 || BB == LatchL1) { continue; }

    for (Instruction &I : *BB) {
      I.replaceUsesOfWith(InductionL1, InductionL0);
    }
  }
}

bool hasBranchSuccessor(BasicBlock *BloccoPartenza, BasicBlock *Successore) {
  if (!BloccoPartenza || !Successore) { return false; }

  BranchInst *Branch = dyn_cast<BranchInst>(BloccoPartenza->getTerminator());

  if (!Branch) { return false; }

  for (unsigned Indice = 0; Indice < Branch->getNumSuccessors(); ++Indice) {
    if (Branch->getSuccessor(Indice) == Successore) { return true; }
  }

  return false;
}

bool replaceBranchSuccessor(BasicBlock *BloccoPartenza, BasicBlock *VecchioSuccessore, BasicBlock *NuovoSuccessore) {
  if (!BloccoPartenza || !VecchioSuccessore || !NuovoSuccessore) { return false; }

  BranchInst *Branch = dyn_cast<BranchInst>(BloccoPartenza->getTerminator());

  if (!Branch) { return false; }

  for (unsigned Indice = 0; Indice < Branch->getNumSuccessors(); ++Indice) {
    if (Branch->getSuccessor(Indice) == VecchioSuccessore) {
      Branch->setSuccessor(Indice, NuovoSuccessore);
      return true;
    }
  }

  return false;
}

BasicBlock *getBody(Loop &L) {
  BasicBlock *Header = L.getHeader();
  BasicBlock *Latch = L.getLoopLatch();

  if (!Header || !Latch) { return nullptr; }

  for (BasicBlock *Successore : successors(Header)) {
    if (L.contains(Successore) && Successore != Latch) { return Successore; }
  }

  return nullptr;
}

bool hasPhiNodes(BasicBlock *BB) {
  if (!BB || BB->empty()) { return false; }

  return isa<PHINode>(&BB->front());
}

bool fuseNonGuardedLoops(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  if (L0.getLoopGuardBranch() || L1.getLoopGuardBranch()) { return false; }

  PHINode *InductionL0 = getInduction(L0, SE);
  PHINode *InductionL1 = getInduction(L1, SE);

  if (!InductionL0 || !InductionL1) { return false; }

  BasicBlock *BodyL0 = getBody(L0);
  BasicBlock *BodyL1 = getBody(L1);
  BasicBlock *LatchL0 = L0.getLoopLatch();
  BasicBlock *LatchL1 = L1.getLoopLatch();
  BasicBlock *ExitingBlockL0 = L0.getExitingBlock();
  BasicBlock *PreheaderL1 = L1.getLoopPreheader();
  BasicBlock *ExitBlockL1 = L1.getExitBlock();

  if (!BodyL0 || !BodyL1 || !LatchL0 || !LatchL1 || !ExitingBlockL0 || !PreheaderL1 || !ExitBlockL1) { return false; }
  if (L0.getExitBlock() != PreheaderL1) { return false; }

  if (hasPhiNodes(BodyL1) || hasPhiNodes(LatchL0) || hasPhiNodes(LatchL1) || hasPhiNodes(ExitBlockL1)) { return false; }

  if (!hasBranchSuccessor(BodyL0, LatchL0)) { return false; }
  if (!hasBranchSuccessor(BodyL1, LatchL1)) { return false; }
  if (!hasBranchSuccessor(ExitingBlockL0, PreheaderL1)) { return false; }

  replaceInductionVariableUses(L1, InductionL0, InductionL1);

  replaceBranchSuccessor(BodyL0, LatchL0, BodyL1);
  replaceBranchSuccessor(BodyL1, LatchL1, LatchL0);
  replaceBranchSuccessor(ExitingBlockL0, PreheaderL1, ExitBlockL1);

  return true;
}

bool fuseGuardedLoops(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  BranchInst *GuardL0 = L0.getLoopGuardBranch();
  BranchInst *GuardL1 = L1.getLoopGuardBranch();

  if (!GuardL0 || !GuardL1 || !haveSameGuard(L0, L1)) { return false; }

  PHINode *InductionL0 = getInduction(L0, SE);
  PHINode *InductionL1 = getInduction(L1, SE);

  if (!InductionL0 || !InductionL1) { return false; }

  BasicBlock *BodyL0 = getBody(L0);
  BasicBlock *BodyL1 = getBody(L1);
  BasicBlock *LatchL0 = L0.getLoopLatch();
  BasicBlock *LatchL1 = L1.getLoopLatch();
  BasicBlock *ExitingBlockL0 = L0.getExitingBlock();
  BasicBlock *EntryL1 = GuardL1->getParent();
  BasicBlock *ExitBlockL1 = L1.getExitBlock();

  if (!BodyL0 || !BodyL1 || !LatchL0 || !LatchL1 || !ExitingBlockL0 || !EntryL1 || !ExitBlockL1) { return false; }
  if (L0.getExitBlock() != EntryL1) { return false; }

  if (hasPhiNodes(BodyL1) || hasPhiNodes(LatchL0) || hasPhiNodes(LatchL1) || hasPhiNodes(EntryL1) || hasPhiNodes(ExitBlockL1)) { return false; }

  if (!hasBranchSuccessor(BodyL0, LatchL0)) { return false; }
  if (!hasBranchSuccessor(BodyL1, LatchL1)) { return false; }
  if (!hasBranchSuccessor(ExitingBlockL0, EntryL1)) { return false; }
  if (!hasBranchSuccessor(GuardL0->getParent(), EntryL1)) { return false; }

  replaceInductionVariableUses(L1, InductionL0, InductionL1);

  replaceBranchSuccessor(BodyL0, LatchL0, BodyL1);
  replaceBranchSuccessor(BodyL1, LatchL1, LatchL0);

  replaceBranchSuccessor(ExitingBlockL0, EntryL1, ExitBlockL1);
  replaceBranchSuccessor(GuardL0->getParent(), EntryL1, ExitBlockL1);

  return true;
}

bool fuseLoops(Loop &L0, Loop &L1, ScalarEvolution &SE) {
  BranchInst *GuardL0 = L0.getLoopGuardBranch();
  BranchInst *GuardL1 = L1.getLoopGuardBranch();

  if (GuardL0 && GuardL1) { return fuseGuardedLoops(L0, L1, SE); }
  if (!GuardL0 && !GuardL1) { return fuseNonGuardedLoops(L0, L1, SE); }

  return false;
}

struct LoopFusionPass : PassInfoMixin<LoopFusionPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
    PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
    ScalarEvolution &SE = AM.getResult<ScalarEvolutionAnalysis>(F);

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

      if (!canFuse(*L0, *L1, DT, PDT, SE)) {
        outs() << "I loop non possono essere fusi\n";
        continue;
      }

      outs() << "I loop possono essere fusi\n";

      if (fuseLoops(*L0, *L1, SE)) {
        IRModificato = true;
        // Worklist e analisi ora sono stale: fondere altre coppie richiede un
        // nuovo run del passo.
        break;
      }
    }

    if (IRModificato) {
      EliminateUnreachableBlocks(F);
      return PreservedAnalyses::none();
    }

    return PreservedAnalyses::all();
  }
};

} // namespace

PassPluginLibraryInfo getLoopFusionPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LoopFusion", LLVM_VERSION_STRING,
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
  return getLoopFusionPluginInfo();
}