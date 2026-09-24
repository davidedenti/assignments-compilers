//-----------------------------------------------------------------------------
// AlgebricIdentity
//-----------------------------------------------------------------------------
// Passo che elimina le operazioni inutili basate su identita' algebriche.
// Scenari ottimizzabili e relativa trasformazione:
/*
  x + 0 == 0 + x --> x
  x * 1 == 1 * x --> x
*/

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/DivisionByConstantInfo.h"

using namespace llvm;

struct AlgebricIdentity : PassInfoMixin<AlgebricIdentity> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &BB : F) {
            // "*It++" legge l'istruzione corrente e avanza subito l'iteratore:
            // cosi' la cancellazione di I non invalida It.
            for (auto It = BB.begin(); It != BB.end();) {
                Instruction &I = *It++;

                // Ci interessano solo le operazioni binarie
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (Op) {
                    // Caso ADD: x + 0 oppure 0 + x
                    if (Op->getOpcode() == Instruction::Add) {
                        Value *Op0 = Op->getOperand(0);
                        Value *Op1 = Op->getOperand(1);

                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isZero()) {
                            // 0 + x --> x
                            Op->replaceAllUsesWith(Op1);
                            Op->eraseFromParent();
                            Changed = true;
                            continue;
                        } else if (C1 && C1->isZero()) {
                            // x + 0 --> x
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                            continue;
                        }
                    }

                    // Caso MUL: x * 1 oppure 1 * x
                    if (Op->getOpcode() == Instruction::Mul) {
                        Value *Op0 = Op->getOperand(0);
                        Value *Op1 = Op->getOperand(1);

                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isOne()) {
                            // 1 * x --> x
                            Op->replaceAllUsesWith(Op1);
                            Op->eraseFromParent();
                            Changed = true;
                        } else if (C1 && C1->isOne()) {
                            // x * 1 --> x
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                        }
                    }
                }
            }
        }

        // none() se l'IR e' stato modificato, all() se e' rimasto invariato
        if (Changed) {
            return PreservedAnalyses::none();
        }

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
                        if (Name == "algebric-identity") {
                            FPM.addPass(AlgebricIdentity());
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