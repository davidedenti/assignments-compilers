//-----------------------------------------------------------------------------
// LoopInvariantMotion implementation
//-----------------------------------------------------------------------------
// Passo che sposta nel preheader le istruzioni loop-invariant.
// Criterio di invarianza e condizione di spostamento:
/*
  invariant: operazione binaria i cui operandi sono definiti fuori dal loop
             oppure da istruzioni gia' marcate come invariant
  hoisting:  solo se l'istruzione domina tutti i blocchi di uscita del loop
*/

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#include <vector>

using namespace llvm;

namespace {

// Basta un operando definito nel loop e non ancora invariant per far fallire
// il controllo.
bool isReachingDefinitionFunction(Instruction &I, Loop &L, std::set<Instruction *> &IstruzioniInvarianti) {
  BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

  if (!Op) {
    return false;
  }

  Value *Op1 = Op->getOperand(0);
  Value *Op2 = Op->getOperand(1);

  Instruction *Definizione1 = dyn_cast<Instruction>(Op1);
  Instruction *Definizione2 = dyn_cast<Instruction>(Op2);

  bool Definizione1DentroLoop = Definizione1 && L.contains(Definizione1->getParent());
  bool Definizione2DentroLoop = Definizione2 && L.contains(Definizione2->getParent());

  if (Definizione1DentroLoop && IstruzioniInvarianti.find(Definizione1) == IstruzioniInvarianti.end()) {
    return false;
  }
  if (Definizione2DentroLoop && IstruzioniInvarianti.find(Definizione2) == IstruzioniInvarianti.end()) {
    return false;
  }

  return true;
}

// Applica LICM a un loop e ai suoi sub-loop: calcola il punto fisso delle
// istruzioni invarianti, poi sposta nel preheader quelle che dominano tutte
// le uscite.
bool processLoop(Loop &L, DominatorTree &DT) {
  bool IRModificato = false;

  // Prima i loop piu' interni, cosi' il codice che diventa invariant a cascata
  // viene gestito quando si arriva al loop esterno.
  for (Loop *SottoLoop : L.getSubLoops()) {
    if (processLoop(*SottoLoop, DT)) {
      IRModificato = true;
    }
  }

  std::set<Instruction *> IstruzioniInvarianti;
  std::vector<Instruction *> IstruzioniInvariantiInOrdine;
  bool NuovaInvariante = false;

  // Si itera finche' non si trovano nuove invarianti (punto fisso)
  do {
    NuovaInvariante = false;

    for (BasicBlock *BB : L.blocks()) {
      for (Instruction &I : *BB) {
        if (isReachingDefinitionFunction(I, L, IstruzioniInvarianti)) {
          auto Inserimento = IstruzioniInvarianti.insert(&I);

          if (Inserimento.second) {
            NuovaInvariante = true;
            IstruzioniInvariantiInOrdine.push_back(&I);

            outs() << "Istruzione potenzialmente loop-invariant: ";
            I.print(outs());
            outs() << "\n";
          }
        }
      }
    }
  } while (NuovaInvariante);

  BasicBlock *Preheader = L.getLoopPreheader();

  if (!Preheader) {
    return IRModificato;
  }

  SmallVector<BasicBlock *, 8> BlocchiUscita;
  L.getExitBlocks(BlocchiUscita);

  for (Instruction *I : IstruzioniInvariantiInOrdine) {
    BasicBlock *BloccoIstruzione = I->getParent();
    bool DominaTutteLeUscite = true;

    // Senza dominanza sulle uscite lo spostamento non e' sicuro
    for (BasicBlock *BloccoUscita : BlocchiUscita) {
      if (!DT.dominates(BloccoIstruzione, BloccoUscita)) {
        DominaTutteLeUscite = false;
        break;
      }
    }

    if (DominaTutteLeUscite) {
      outs() << "Sposto nel preheader: ";
      I->print(outs());
      outs() << "\n";

      I->moveBefore(Preheader->getTerminator());
      IRModificato = true;
    }
  }

  return IRModificato;
}

// Entry point: i sub-loop sono gestiti dalla ricorsione dentro processLoop
struct LoopInvariantMotion : PassInfoMixin<LoopInvariantMotion> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
    bool IRModificato = false;

    for (Loop *L : LI) {
      if (processLoop(*L, DT)) {
        IRModificato = true;
      }
    }

    if (IRModificato) {
      return PreservedAnalyses::none();
    }

    return PreservedAnalyses::all();
  }
};

}

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
PassPluginLibraryInfo getLocalOptsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
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