//-----------------------------------------------------------------------------
// MultiInstructionOptimization
//-----------------------------------------------------------------------------
// Passo che riconosce una coppia add/sub con la stessa costante e la semplifica.
// Scenario ottimizzabile e relativa trasformazione:
/*
  a = b + C ; c = a - C  -->  c = b
*/
// La ricerca parte dalla sub e guarda all'indietro il suo primo operando.

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/DivisionByConstantInfo.h"

using namespace llvm;

struct MultiInstructionOptimization : PassInfoMixin<MultiInstructionOptimization> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;

                // Partiamo dalla sub
                BinaryOperator *Sub = dyn_cast<BinaryOperator>(&I);
                if (!Sub) { continue; }
                if (Sub->getOpcode() != Instruction::Sub) { continue; }

                Value *SubOp0 = Sub->getOperand(0); // minuendo (a)
                Value *SubOp1 = Sub->getOperand(1); // sottraendo (deve essere C)

                // Il sottraendo deve essere una costante intera
                ConstantInt *SubConstant = dyn_cast<ConstantInt>(SubOp1);
                if (!SubConstant) { continue; }

                // Il minuendo deve essere il risultato di una add
                BinaryOperator *Add = dyn_cast<BinaryOperator>(SubOp0);
                if (!Add) { continue; }
                if (Add->getOpcode() != Instruction::Add) { continue; }

                // Scelta conservativa: con nsw/nuw rinunciamo, per non dover
                // ragionare sulla semantica del poison in caso di overflow.
                if (Add->hasNoSignedWrap() || Add->hasNoUnsignedWrap()) { continue; }
                if (Sub->hasNoSignedWrap() || Sub->hasNoUnsignedWrap()) { continue; }

                Value *AddOp0 = Add->getOperand(0);
                Value *AddOp1 = Add->getOperand(1);

                // La add e' commutativa: la costante puo' stare in entrambe le posizioni
                ConstantInt *AddConstant0 = dyn_cast<ConstantInt>(AddOp0);
                ConstantInt *AddConstant1 = dyn_cast<ConstantInt>(AddOp1);
                ConstantInt *AddConstant = nullptr; // costante della add, se combacia con C
                Value *OriginalValue = nullptr;      // operando non costante (b)

                // Caso C + b
                if(AddConstant0 && AddConstant0->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant0;
                    OriginalValue = AddOp1;
                }
                // Caso b + C
                if(AddConstant1 && AddConstant1->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant1;
                    OriginalValue = AddOp0;
                }

                // Costanti diverse: non e' lo schema cercato
                if(!AddConstant) continue;

                // (b + C) - C --> b
                Sub->replaceAllUsesWith(OriginalValue);
                Sub->eraseFromParent();

                // La add si elimina solo se "a" non serve altrove
                if (Add->use_empty()) { Add->eraseFromParent(); }
                Changed = true;
            }
        }

        if (Changed) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
};

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
PassPluginLibraryInfo getTestPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "LocalOpts",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "multi-instruction-optimization") {
                            FPM.addPass(MultiInstructionOptimization());
                            return true;
                        }

                        return false;
                    }
            );
        }
    };
}

// Interfaccia richiesta da opt per caricare il plugin (-load-pass-plugin)
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}