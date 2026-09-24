//-----------------------------------------------------------------------------
// StrengthReduction
//-----------------------------------------------------------------------------
// Passo che sostituisce mul e udiv per costante con operazioni piu' economiche.
// Scenari ottimizzabili e relativa trasformazione:
/*
  x * 8   --> x << 3            (costante potenza di 2)
  x * 15  --> (x << 4) - x      (costante del tipo 2^k - 1)
  x * C   --> somma di shift    (caso generale, un termine per ogni bit a 1)
  x / 8   --> x >> 3            (divisore potenza di 2)
*/
// La trasformazione viene applicata solo se un modello di costo indicativo
// dice che la sequenza sostitutiva costa meno dell'operazione originale.

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

// Costo indicativo di ogni tipo di istruzione: mul cara, div carissima,
// shift/add/sub economici.
static unsigned getInstructionCost(unsigned Opcode) {
    if (Opcode == Instruction::Mul) { return 3; }
    if (Opcode == Instruction::UDiv) { return 10; }
    if (Opcode == Instruction::Shl || Opcode == Instruction::LShr) { return 1; }
    if (Opcode == Instruction::Add || Opcode == Instruction::Sub) { return 1; }
    return 1;
}

// Conviene solo se la sostituzione costa meno dell'operazione originale
static bool isProfitable(unsigned OriginalOpcode, unsigned ReplacementCost) {
    return ReplacementCost < getInstructionCost(OriginalOpcode);
}

// Costo della sostituzione di x * Constant con shift/add/sub, senza creare
// ancora le istruzioni: serve solo a decidere se conviene.
static unsigned getMultiplyCost(const APInt &Constant) {
    APInt Magnitude = Constant.abs(); // il segno si gestisce alla fine
    unsigned Cost = 0;
    unsigned Terms = 0;

    if (Magnitude.isOne()) {
        // x * 1 = x
        Cost = 0;
    } else if (Magnitude.isPowerOf2()) {
        // x * 2^k = x << k
        Cost = getInstructionCost(Instruction::Shl);
    } else if ((Magnitude + 1).isPowerOf2()) {
        // x * (2^k - 1) = (x << k) - x
        Cost = getInstructionCost(Instruction::Shl) + getInstructionCost(Instruction::Sub);
    } else {
        // Caso generale: un termine x << Bit per ogni bit a 1, sommati tra loro
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; }

            if (Bit != 0) { Cost += getInstructionCost(Instruction::Shl); } // x << 0 = x
            if (Terms != 0) { Cost += getInstructionCost(Instruction::Add); } // somma ai precedenti

            Terms++;
        }
    }

    // Costante negativa: serve una sub finale per il cambio di segno
    if (Constant.isNegative()) { Cost += getInstructionCost(Instruction::Sub); }

    return Cost;
}

// Costruisce le istruzioni che calcolano ValueToMultiply * Constant con soli
// shift/add/sub, seguendo la stessa casistica di getMultiplyCost.
static Value *createMultiplyByConstant(IRBuilder<> &Builder, Value *ValueToMultiply, const APInt &Constant) {
    APInt Magnitude = Constant.abs();
    Value *Result = nullptr;

    if (Magnitude.isOne()) {
        // x * 1 = x
        Result = ValueToMultiply;
    } else if (Magnitude.isPowerOf2()) {
        // x * 2^k = x << k
        Result = Builder.CreateShl(ValueToMultiply, Magnitude.logBase2(), "strength.shift");
    } else if ((Magnitude + 1).isPowerOf2()) {
        // x * (2^k - 1) = (x << k) - x
        Value *ShiftedValue = Builder.CreateShl(ValueToMultiply, (Magnitude + 1).logBase2(), "strength.shift");
        Result = Builder.CreateSub(ShiftedValue, ValueToMultiply, "strength.sub");
    } else {
        // Caso generale: somma di x << Bit per ogni bit a 1 della costante
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; }

            Value *Term = ValueToMultiply;

            if (Bit != 0) {
                Term = Builder.CreateShl(ValueToMultiply, Bit, "strength.shift");
            }

            if (!Result) {
                Result = Term; // primo termine
            } else {
                Result = Builder.CreateAdd(Result, Term, "strength.add"); // accumulo
            }
        }
    }

    // Costante negativa: 0 - Result
    if (Constant.isNegative()) {
        Result = Builder.CreateSub(ConstantInt::get(ValueToMultiply->getType(), 0), Result, "strength.neg");
    }

    return Result;
}

struct StrengthReduction : PassInfoMixin<StrengthReduction> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (!Op) { continue; }

                //====================== A) MUL per costante ======================
                if (Op->getOpcode() == Instruction::Mul) {
                    // Scelta conservativa sui flag nsw/nuw, come nel passo 2
                    if (Op->hasNoSignedWrap() || Op->hasNoUnsignedWrap()) { continue; }

                    // Individuiamo quale dei due operandi e' la costante
                    ConstantInt *C0 = dyn_cast<ConstantInt>(Op->getOperand(0));
                    ConstantInt *C1 = dyn_cast<ConstantInt>(Op->getOperand(1));
                    ConstantInt *Constant = nullptr;
                    Value *OriginalValue = nullptr;

                    if (C0) {
                        Constant = C0;
                        OriginalValue = Op->getOperand(1);
                    } else if (C1) {
                        Constant = C1;
                        OriginalValue = Op->getOperand(0);
                    }

                    // x * 0 e' un'altra semplificazione, non di questo passo
                    if (!Constant || Constant->isZero()) { continue; }

                    unsigned ReplacementCost = getMultiplyCost(Constant->getValue());

                    if (!isProfitable(Instruction::Mul, ReplacementCost)) { continue; }

                    // Le nuove istruzioni vengono inserite prima di Op
                    IRBuilder<> Builder(Op);
                    Value *Replacement = createMultiplyByConstant(Builder, OriginalValue, Constant->getValue());

                    Op->replaceAllUsesWith(Replacement);
                    Op->eraseFromParent();
                    Changed = true;
                    continue;
                }

                //================= B) UDIV per costante =================
                if (Op->getOpcode() != Instruction::UDiv) { continue; }
                // "exact" promette divisione senza resto: caso non gestito
                if (Op->isExact()) { continue; }

                Value *Dividend = Op->getOperand(0);
                ConstantInt *Divisor = dyn_cast<ConstantInt>(Op->getOperand(1));

                // Divisore costante, diverso da 0 (indefinito) e da 1 (x / 1 = x)
                if (!Divisor || Divisor->isZero() || Divisor->isOne()) { continue; }

                const APInt &DivisorValue = Divisor->getValue();

                // Si gestiscono solo le potenze di 2: x / 2^k = x >> k
                // Gli altri divisori restano una udiv
                if (!DivisorValue.isPowerOf2()) { continue; }

                unsigned Shift = DivisorValue.logBase2();

                if (!isProfitable(Instruction::UDiv, getInstructionCost(Instruction::LShr))) { continue; }

                IRBuilder<> Builder(Op);
                Value *Replacement = Builder.CreateLShr(Dividend, Shift, "strength.lshr");

                Op->replaceAllUsesWith(Replacement);
                Op->eraseFromParent();
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
                        if (Name == "strength-reduction") {
                            FPM.addPass(StrengthReduction());
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