//===----------------------------------------------------------------------===//
// Terzo assignment di "Compilatori - Middle end"
// Loop-Invariant Code Motion
//
// Il nome del passo NON usa l'acronimo "LICM", perché LLVM ne possiede già
// uno ufficiale. Il passo è invocabile con:
//   opt -load-pass-plugin=./LocalOpts.so -passes=loop-invariant-motion ...
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

namespace {


  bool isReachingDefinitionFunction(Instruction &I, Loop &L, std::set<Instruction *> &IstruzioniInvarianti) {
    BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

    if (!Op) { return false; }

    Value *Op1 = Op->getOperand(0);
    Value *Op2 = Op->getOperand(1);

    Instruction *Definizione1 = dyn_cast<Instruction>(Op1);
    Instruction *Definizione2 = dyn_cast<Instruction>(Op2);

    bool Definizione1DentroLoop = Definizione1 && L.contains(Definizione1->getParent());
    bool Definizione2DentroLoop = Definizione2 && L.contains(Definizione2->getParent());

    if (Definizione1DentroLoop && IstruzioniInvarianti.find(Definizione1) == IstruzioniInvarianti.end()) { return false; }

    if (Definizione2DentroLoop && IstruzioniInvarianti.find(Definizione2) == IstruzioniInvarianti.end()) { return false; }

    return true;
  }

struct LoopInvariantMotion : PassInfoMixin<LoopInvariantMotion> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    bool Changed = false;

    // TODO Assignment 3
    // Per ciascun loop, usare LI e DT per:
    //  1. individuare le istruzioni loop-invariant;
    //  2. verificare le condizioni di correttezza per lo spostamento;
    //  3. spostare le istruzioni candidate nel preheader del loop.
    //
    // Punti di partenza utili:
    //   Loop::getLoopPreheader(), Loop::contains(), Loop::getExitBlocks()
    //   DominatorTree::dominates(), Instruction::moveBefore().
    for (Loop *L : LI) {
      std::set<Instruction *> IstruzioniInvarianti;
      bool NuovaInvariante = false;

      do {
        NuovaInvariante = false;

        for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {
            if (isReachingDefinitionFunction(I, *L, IstruzioniInvarianti)) {
              auto Inserimento = IstruzioniInvarianti.insert(&I);

              if (Inserimento.second) {
                NuovaInvariante = true;
                outs() << "Istruzione potenzialmente loop-invariant: ";
                I.print(outs());
                outs() << "\n";
              }
            }
          }
        }
      } while (NuovaInvariante);
    }

    (void)DT;

    // Cambiare in PreservedAnalyses::none() appena il pass modificherà l'IR.
    return PreservedAnalyses::all();
  }
};

} // namespace

PassPluginLibraryInfo getLocalOptsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "loop-invariant-motion") {
                    FPM.addPass(LoopInvariantMotion());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getLocalOptsPluginInfo();
}
