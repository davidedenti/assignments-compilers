//===----------------------------------------------------------------------===//
//  LocalOpts.cpp  —  Primo Assignment di "Compilatori - Middle end"
//
//  Questo file implementa TRE ottimizzazioni (tre "passi" LLVM) che lavorano
//  sul codice intermedio (IR = Intermediate Representation):
//
//    1) AlgebricIdentity           ->  x + 0 = x   e   x * 1 = x
//    2) MultiInstructionOptimization ->  a = b + C ; c = a - C   =>   c = b
//    3) StrengthReduction          ->  sostituisce moltiplicazioni e divisioni
//                                      per costante con operazioni piu' veloci
//                                      (shift, addizioni, sottrazioni)
//
//  IDEE DI BASE CHE SERVONO PER CAPIRE TUTTO IL FILE
//  -------------------------------------------------
//  - L'IR di LLVM e' organizzato cosi':
//        Function  ->  contiene Basic Block
//        BasicBlock ->  contiene Instruction (in ordine)
//        Instruction ->  produce un valore (Value) e usa altri Value come operandi
//
//  - Un "Value" e' qualunque cosa possa essere usata come operando: il risultato
//    di un'istruzione, un argomento della funzione, una costante, ecc.
//
//  - Ogni volta che un'istruzione usa il risultato di un'altra, si crea una
//    relazione di "uso" (Use). replaceAllUsesWith() serve a dire:
//    "tutti quelli che usavano ME, d'ora in poi usino QUEST'ALTRO valore".
//
//  - Un "passo" (Pass) e' una classe che LLVM esegue su ogni funzione. Deve
//    dire alla fine se ha modificato qualcosa, tramite il valore di ritorno
//    PreservedAnalyses (vedi sotto).
//===----------------------------------------------------------------------===//

// ---- Header di LLVM: ognuno porta un pezzo dell'API che usiamo ----
#include "llvm/IR/Constants.h"       // ConstantInt (le costanti intere)
#include "llvm/IR/Function.h"        // Function, BasicBlock
#include "llvm/IR/IRBuilder.h"       // IRBuilder: crea nuove istruzioni comodamente
#include "llvm/IR/Instructions.h"    // BinaryOperator e le altre istruzioni
#include "llvm/IR/PassManager.h"     // PassInfoMixin, PreservedAnalyses (New Pass Manager)
#include "llvm/Passes/PassBuilder.h" // per registrare il passo nella pipeline
#include "llvm/Passes/PassPlugin.h"  // per compilare tutto come plugin caricabile
#include "llvm/Support/DivisionByConstantInfo.h" // i "numeri magici" per la divisione

using namespace llvm; // cosi' non dobbiamo scrivere "llvm::" davanti a tutto


//===----------------------------------------------------------------------===//
//  PASSO 2 - MultiInstructionOptimization
//
//  Riconosce la sequenza:
//        a = b + C        (una addizione con una costante C)
//        c = a - C        (una sottrazione della STESSA costante C)
//  e la semplifica in:
//        c = b            (perche' (b + C) - C = b)
//
//  In pratica cerchiamo una SUB e guardiamo "all'indietro" se il suo primo
//  operando e' una ADD compatibile.
//===----------------------------------------------------------------------===//
struct MultiInstructionOptimization : PassInfoMixin<MultiInstructionOptimization> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++; // stesso trucco di prima per iterare in sicurezza

                // Partiamo dalla SUB. Se l'istruzione non e' una sub, la saltiamo.
                BinaryOperator *Sub = dyn_cast<BinaryOperator>(&I);
                if (!Sub) { continue; }
                if (Sub->getOpcode() != Instruction::Sub) { continue; }

                Value *SubOp0 = Sub->getOperand(0); // il "minuendo"  (a)
                Value *SubOp1 = Sub->getOperand(1); // il "sottraendo" (deve essere C)

                // Il sottraendo deve essere una costante intera, altrimenti niente da fare.
                ConstantInt *SubConstant = dyn_cast<ConstantInt>(SubOp1);
                if (!SubConstant) { continue; }

                // Il primo operando della sub (a) deve essere il RISULTATO di una add.
                BinaryOperator *Add = dyn_cast<BinaryOperator>(SubOp0);
                if (!Add) { continue; }
                if (Add->getOpcode() != Instruction::Add) { continue; }

                // ---- Controllo di sicurezza sui flag nsw/nuw ----
                // "nsw" = no signed wrap, "nuw" = no unsigned wrap. Sono promesse
                // che dicono "questa operazione NON va in overflow"; se lo fa,
                // il risultato e' "poison" (valore avvelenato/indefinito).
                // Per non complicarci la vita con la semantica del poison, se
                // add o sub hanno uno di questi flag, rinunciamo (scelta prudente).
                // NB: qui questo rende il passo conservativo (a volte non scatta),
                //     ma non lo rende MAI sbagliato.
                if (Add->hasNoSignedWrap() || Add->hasNoUnsignedWrap()) { continue; }
                if (Sub->hasNoSignedWrap() || Sub->hasNoUnsignedWrap()) { continue; }

                Value *AddOp0 = Add->getOperand(0);
                Value *AddOp1 = Add->getOperand(1);

                // La add puo' essere scritta come  b + C  oppure  C + b (commutativa),
                // quindi controlliamo entrambe le posizioni per la costante.
                ConstantInt *AddConstant0 = dyn_cast<ConstantInt>(AddOp0);
                ConstantInt *AddConstant1 = dyn_cast<ConstantInt>(AddOp1);
                ConstantInt *AddConstant = nullptr; // qui finira' la costante della add, se combacia
                Value *OriginalValue = nullptr;      // qui finira' "b" (il valore non costante)

                // Caso  C + b : la costante e' il primo operando; b e' il secondo.
                // La costante della add deve essere UGUALE a quella della sub.
                if(AddConstant0 && AddConstant0->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant0;
                    OriginalValue = AddOp1; // b
                }
                // Caso  b + C : la costante e' il secondo operando; b e' il primo.
                if(AddConstant1 && AddConstant1->getValue() == SubConstant->getValue()) {
                    AddConstant = AddConstant1;
                    OriginalValue = AddOp0; // b
                }

                // Se nessun operando della add combacia con C, non e' il nostro schema.
                if(!AddConstant) continue;

                // Trasformazione:  (b + C) - C  =>  b
                // Tutti quelli che usavano la sub ora useranno direttamente b.
                Sub->replaceAllUsesWith(OriginalValue);
                Sub->eraseFromParent();

                // Pulizia: se la add non e' piu' usata da NESSUNO (era usata solo
                // dalla sub che abbiamo appena tolto), possiamo cancellarla.
                // Se invece "a" serviva anche altrove, la lasciamo dov'e'.
                if (Add->use_empty()) { Add->eraseFromParent(); }
                Changed = true;
            }
        }

        if (Changed) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
};

//===----------------------------------------------------------------------===//
//  REGISTRAZIONE DEL PLUGIN
//
//  Questa parte "presenta" i nostri passi a LLVM, in modo da poterli richiamare
//  dalla riga di comando con opt, ad esempio:
//      opt -load-pass-plugin=./LocalOpts.so -passes=strength-reduction ...
//
//  registerPipelineParsingCallback riceve il NOME scritto dall'utente dopo
//  -passes= e, se lo riconosce, aggiunge il passo corrispondente alla pipeline.
//===----------------------------------------------------------------------===//
PassPluginLibraryInfo getTestPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, // versione dell'API dei plugin
        "LocalOpts",             // nome del plugin
        LLVM_VERSION_STRING,     // versione di LLVM con cui e' compilato
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                        // Confrontiamo il nome richiesto con i nostri tre passi.
                        if (Name == "multi-instruction-optimization") {
                            FPM.addPass(MultiInstructionOptimization());
                            return true;
                        }

                        return false; // nome non nostro: lascia decidere ad altri
                    }
            );
        }
    };
}

// Punto d'ingresso che LLVM cerca quando carica il plugin (.so).
// LLVM_ATTRIBUTE_WEAK permette a piu' plugin di coesistere senza conflitti.
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}