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

using namespace llvm;

namespace {

struct LoopInvariantMotion : PassInfoMixin<LoopInvariantMotion> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);

    // TODO Assignment 3
    // Per ciascun loop, usare LI e DT per:
    //  1. individuare le istruzioni loop-invariant;
    //  2. verificare le condizioni di correttezza per lo spostamento;
    //  3. spostare le istruzioni candidate nel preheader del loop.
    //
    // Punti di partenza utili:
    //   Loop::getLoopPreheader(), Loop::contains(), Loop::getExitBlocks()
    //   DominatorTree::dominates(), Instruction::moveBefore().
    (void)LI;
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
