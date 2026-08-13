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

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (Op) {
                    if (Op->getOpcode() == Instruction::Add) {
                        Value *Op0 = Op->getOperand(0);
                        Value *Op1 = Op->getOperand(1);

                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isZero()) {
                            Op->replaceAllUsesWith(Op1);
                            Op->eraseFromParent();
                            Changed = true;
                            continue;
                        } else if (C1 && C1->isZero()) {
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                            continue;
                        }
                    }

                    if (Op->getOpcode() == Instruction::Mul) {
                        Value *Op0 = Op->getOperand(0);
                        Value *Op1 = Op->getOperand(1);

                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isOne()) {
                            Op->replaceAllUsesWith(Op1);
                            Op->eraseFromParent();
                            Changed = true;
                        } else if (C1 && C1->isOne()) {
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                        }
                    }
                }
            }
        }

        if (Changed) {
            return PreservedAnalyses::none();
        }

        return PreservedAnalyses::all();
    }
};

struct MultiInstructionOptimization : PassInfoMixin<MultiInstructionOptimization> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;
                BinaryOperator *Sub = dyn_cast<BinaryOperator>(&I);
                if (!Sub) { continue; }
                if (Sub->getOpcode() != Instruction::Sub) { continue; }

                Value *SubOp0 = Sub->getOperand(0);
                Value *SubOp1 = Sub->getOperand(1);

                ConstantInt *SubConstant = dyn_cast<ConstantInt>(SubOp1);
                if (!SubConstant) { continue; }

                BinaryOperator *Add = dyn_cast<BinaryOperator>(SubOp0);
                if (!Add) { continue; }
                if (Add->getOpcode() != Instruction::Add) { continue; }
                if (Add->hasNoSignedWrap() || Add->hasNoUnsignedWrap()) { continue; }
                if (Sub->hasNoSignedWrap() || Sub->hasNoUnsignedWrap()) { continue; }

                Value *AddOp0 = Add->getOperand(0);
                Value *AddOp1 = Add->getOperand(1);


                ConstantInt *AddConstant0 = dyn_cast<ConstantInt>(AddOp0);
                ConstantInt *AddConstant1 = dyn_cast<ConstantInt>(AddOp1);
                ConstantInt *AddConstant = nullptr;
                Value *OriginalValue = nullptr;

                if(AddConstant0 && AddConstant0->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant0;
                    OriginalValue = AddOp1;
                }
                if(AddConstant1 && AddConstant1->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant1;
                    OriginalValue = AddOp0;
                }
                if(!AddConstant) continue;
                Sub->replaceAllUsesWith(OriginalValue);
                Sub->eraseFromParent();
                if (Add->use_empty()) { Add->eraseFromParent(); }
                Changed = true;

            }
        }

        if (Changed) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
};

static unsigned getInstructionCost(unsigned Opcode) {
    if (Opcode == Instruction::Mul) { return 3; }
    if (Opcode == Instruction::UDiv) { return 10; }
    if (Opcode == Instruction::Shl || Opcode == Instruction::LShr) { return 1; }
    if (Opcode == Instruction::Add || Opcode == Instruction::Sub) { return 1; }
    return 1;
}

static bool isProfitable(unsigned OriginalOpcode, unsigned ReplacementCost) {
    return ReplacementCost < getInstructionCost(OriginalOpcode);
}

static unsigned getMultiplyCost(const APInt &Constant) {
    APInt Magnitude = Constant.abs();
    unsigned Cost = 0;
    unsigned Terms = 0;

    if (Magnitude.isOne()) {
        Cost = 0;
    } else if (Magnitude.isPowerOf2()) {
        Cost = getInstructionCost(Instruction::Shl);
    } else if ((Magnitude + 1).isPowerOf2()) {
        Cost = getInstructionCost(Instruction::Shl) + getInstructionCost(Instruction::Sub);
    } else {
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; }

            if (Bit != 0) { Cost += getInstructionCost(Instruction::Shl); }
            if (Terms != 0) { Cost += getInstructionCost(Instruction::Add); }

            Terms++;
        }
    }

    if (Constant.isNegative()) { Cost += getInstructionCost(Instruction::Sub); }

    return Cost;
}

static Value *createMultiplyByConstant(IRBuilder<> &Builder, Value *ValueToMultiply, const APInt &Constant) {
    APInt Magnitude = Constant.abs();
    Value *Result = nullptr;

    if (Magnitude.isOne()) {
        Result = ValueToMultiply;
    } else if (Magnitude.isPowerOf2()) {
        Result = Builder.CreateShl(ValueToMultiply, Magnitude.logBase2(), "strength.shift");
    } else if ((Magnitude + 1).isPowerOf2()) {
        Value *ShiftedValue = Builder.CreateShl(ValueToMultiply, (Magnitude + 1).logBase2(), "strength.shift");
        Result = Builder.CreateSub(ShiftedValue, ValueToMultiply, "strength.sub");
    } else {
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; }

            Value *Term = ValueToMultiply;

            if (Bit != 0) {
                Term = Builder.CreateShl(ValueToMultiply, Bit, "strength.shift");
            }

            if (!Result) {
                Result = Term;
            } else {
                Result = Builder.CreateAdd(Result, Term, "strength.add");
            }
        }
    }

    if (Constant.isNegative()) {
        Result = Builder.CreateSub(ConstantInt::get(ValueToMultiply->getType(), 0), Result, "strength.neg");
    }

    return Result;
}

static Value *createUnsignedMultiplyHigh(IRBuilder<> &Builder, Value *Dividend, const APInt &Magic) {
    IntegerType *Type = dyn_cast<IntegerType>(Dividend->getType());
    unsigned BitWidth = Type->getBitWidth();
    IntegerType *WideType = IntegerType::get(Builder.getContext(), BitWidth * 2);

    Value *WideDividend = Builder.CreateZExt(Dividend, WideType, "magic.dividend");
    Value *WideMagic = ConstantInt::get(WideType, Magic.zext(BitWidth * 2));
    Value *Product = Builder.CreateMul(WideDividend, WideMagic, "magic.product");
    Value *HighHalf = Builder.CreateLShr(Product, BitWidth, "magic.high");

    return Builder.CreateTrunc(HighHalf, Type, "magic.quotient");
}

static unsigned getMagicDivisionCost(const UnsignedDivisionByConstantInfo &Magic) {
    unsigned Cost = getInstructionCost(Instruction::Mul) + getInstructionCost(Instruction::LShr);

    if (Magic.PreShift != 0) { Cost += getInstructionCost(Instruction::LShr); }

    if (Magic.IsAdd) {
        Cost += getInstructionCost(Instruction::Sub);
        Cost += getInstructionCost(Instruction::LShr);
        Cost += getInstructionCost(Instruction::Add);
    }

    if (Magic.PostShift != 0) { Cost += getInstructionCost(Instruction::LShr); }

    return Cost;
}

struct StrengthReduction : PassInfoMixin<StrengthReduction> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (!Op) { continue; }

                if (Op->getOpcode() == Instruction::Mul) {
                    if (Op->hasNoSignedWrap() || Op->hasNoUnsignedWrap()) { continue; }

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

                    if (!Constant || Constant->isZero()) { continue; }

                    unsigned ReplacementCost = getMultiplyCost(Constant->getValue());

                    if (!isProfitable(Instruction::Mul, ReplacementCost)) { continue; }

                    IRBuilder<> Builder(Op);
                    Value *Replacement = createMultiplyByConstant(Builder, OriginalValue, Constant->getValue());

                    Op->replaceAllUsesWith(Replacement);
                    Op->eraseFromParent();
                    Changed = true;
                    continue;
                }

                if (Op->getOpcode() != Instruction::UDiv) { continue; }
                if (Op->isExact()) { continue; }

                Value *Dividend = Op->getOperand(0);
                ConstantInt *Divisor = dyn_cast<ConstantInt>(Op->getOperand(1));

                if (!Divisor || Divisor->isZero() || Divisor->isOne()) { continue; }

                const APInt &DivisorValue = Divisor->getValue();
                IRBuilder<> Builder(Op);

                if (DivisorValue.isPowerOf2()) {
                    unsigned Shift = DivisorValue.logBase2();

                    if (!isProfitable(Instruction::UDiv, getInstructionCost(Instruction::LShr))) { continue; }

                    Value *Replacement = Builder.CreateLShr(Dividend, Shift, "strength.lshr");

                    Op->replaceAllUsesWith(Replacement);
                    Op->eraseFromParent();
                    Changed = true;
                    continue;
                }

                UnsignedDivisionByConstantInfo Magic = UnsignedDivisionByConstantInfo::get(DivisorValue);
                unsigned ReplacementCost = getMagicDivisionCost(Magic);

                if (!isProfitable(Instruction::UDiv, ReplacementCost)) { continue; }

                Value *AdjustedDividend = Dividend;

                if (Magic.PreShift != 0) {
                    AdjustedDividend = Builder.CreateLShr(AdjustedDividend, Magic.PreShift, "magic.preshift");
                }

                Value *Quotient = createUnsignedMultiplyHigh(Builder, AdjustedDividend, Magic.Magic);

                if (Magic.IsAdd) {
                    Value *Difference = Builder.CreateSub(AdjustedDividend, Quotient, "magic.difference");
                    Value *HalfDifference = Builder.CreateLShr(Difference, 1, "magic.half");
                    Quotient = Builder.CreateAdd(Quotient, HalfDifference, "magic.adjusted");
                }

                if (Magic.PostShift != 0) {
                    Quotient = Builder.CreateLShr(Quotient, Magic.PostShift, "magic.postshift");
                }

                Op->replaceAllUsesWith(Quotient);
                Op->eraseFromParent();
                Changed = true;
            }
        }

        if (Changed) { return PreservedAnalyses::none(); }
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
                    [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "algebric-identity") {
                            FPM.addPass(AlgebricIdentity());
                            return true;
                        }
                        if (Name == "multi-instruction-optimization") {
                            FPM.addPass(MultiInstructionOptimization());
                            return true;
                        }
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

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}
