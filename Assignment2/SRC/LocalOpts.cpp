#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CFG.h"
#include <cstddef>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>



using namespace llvm;
using ExpressionSet = std::set<std::string>;

struct StatoDataFlow {
    ExpressionSet In;
    ExpressionSet Out;
};




bool isChanged(std::vector <StatoDataFlow> prev, std::vector <StatoDataFlow> curr) {

    for (int i = 0; i < prev.size(); i++) {
        if (prev.at(i).In != curr.at(i).In) return true;
        if (prev.at(i).Out != curr.at(i).Out) return true;

    }
    return false;

}



struct VeryBusyExpressions : PassInfoMixin<VeryBusyExpressions> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;
        int IterationsCounter = 0;
        std::vector<std::vector<StatoDataFlow>> Iterazioni;

        do{

            std::unordered_map<BasicBlock *, int> BlockIndex;
            int Index = 0;

            for (BasicBlock &BB : llvm::reverse(F)) {
                StatoDataFlow Data;
                BlockIndex[&BB] = Index;

                if (succ_empty(&BB)) {
                    Data.Out = nullptr;
                }
                else {
                    ExpressionSet Intersection;
                    bool PrimoSuccessore = true;

                    for (BasicBlock *Successore : successors(&BB)) {
                        const ExpressionSet &InSuccessore = Iterazioni.at(IterationsCounter).at(BlockIndex.at(Successore)).In;

                        if (PrimoSuccessore) {
                            Intersection = InSuccessore;
                            PrimoSuccessore = false;
                        } else {
                            ExpressionSet NuovaIntersection;

                            for (const std::string &Espressione : Intersection) {
                                if (InSuccessore.find(Espressione) != InSuccessore.end()) {
                                    NuovaIntersection.insert(Espressione);
                                }
                            }

                            Intersection = NuovaIntersection;
                        }
                    }

                    Data.In = Intersection;

                }


            }

            if (Iterazioni.size() == 1)  Changed = true;
            else Changed = isChanged(Iterazioni.at(IterationsCounter - 1), Iterazioni.at(IterationsCounter));

            IterationsCounter++;
        }while (Changed);


        return PreservedAnalyses::all();
    }
};

PassPluginLibraryInfo getTestPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "LocalOpts",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "very-busy-expressions") {
                            FPM.addPass(VeryBusyExpressions());
                            return true;
                        }

                        return false;
                    }
            );
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}
