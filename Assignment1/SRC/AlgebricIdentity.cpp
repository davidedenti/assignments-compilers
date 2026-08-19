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
//  PASSO 1 - AlgebricIdentity
//
//  Elimina operazioni inutili basate su identita' algebriche:
//      x + 0  ->  x        (e anche 0 + x -> x)
//      x * 1  ->  x        (e anche 1 * x -> x)
//
//  "PassInfoMixin<...>" e' solo boilerplate del New Pass Manager: rende questa
//  struct un passo valido. L'unica cosa importante che dobbiamo scrivere e' run().
//===----------------------------------------------------------------------===//
struct AlgebricIdentity : PassInfoMixin<AlgebricIdentity> {
    // run() viene chiamata da LLVM una volta per ogni Function.
    // Il secondo parametro (FunctionAnalysisManager) qui non ci serve, quindi
    // non gli diamo nemmeno un nome.
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false; // diventa true se modifichiamo almeno un'istruzione

        // Scorriamo tutti i Basic Block della funzione...
        for (BasicBlock &B : F) {
            // ...e dentro ogni blocco scorriamo tutte le istruzioni.
            //
            // ATTENZIONE al modo in cui iteriamo. Piu' avanti potremmo CANCELLARE
            // l'istruzione corrente. Se cancelliamo l'elemento su cui e' puntato
            // l'iteratore, l'iteratore diventa invalido (crash).
            // Trucco: "*It++" fa DUE cose in un colpo solo:
            //    1) legge l'istruzione puntata da It  (la mettiamo in I)
            //    2) POI avanza It all'istruzione successiva
            // Cosi', quando cancelliamo I, l'iteratore It punta gia' oltre ed e' salvo.
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;

                // Ci interessano solo le operazioni binarie (add, sub, mul, ...).
                // dyn_cast<T> prova a "vedere" I come un BinaryOperator:
                //   - se I e' davvero un BinaryOperator, restituisce il puntatore
                //   - altrimenti restituisce nullptr
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (Op) {
                    // ---- Caso ADDIZIONE: cerchiamo  x + 0  oppure  0 + x ----
                    if (Op->getOpcode() == Instruction::Add) {
                        Value *Op0 = Op->getOperand(0); // primo operando
                        Value *Op1 = Op->getOperand(1); // secondo operando

                        // Proviamo a vedere ciascun operando come costante intera.
                        // Se non lo e', C0/C1 saranno nullptr.
                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isZero()) {
                            // Forma  0 + x : il risultato e' semplicemente x (= Op1).
                            // Diciamo a chiunque usasse questa add di usare Op1.
                            Op->replaceAllUsesWith(Op1);
                            // Ora la add non serve piu': la cancelliamo dal blocco.
                            Op->eraseFromParent();
                            Changed = true;
                            continue; // passa all'istruzione successiva
                        } else if (C1 && C1->isZero()) {
                            // Forma  x + 0 : il risultato e' x (= Op0).
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                            continue;
                        }
                    }

                    // ---- Caso MOLTIPLICAZIONE: cerchiamo  x * 1  oppure  1 * x ----
                    if (Op->getOpcode() == Instruction::Mul) {
                        Value *Op0 = Op->getOperand(0);
                        Value *Op1 = Op->getOperand(1);

                        ConstantInt *C0 = dyn_cast<ConstantInt>(Op0);
                        ConstantInt *C1 = dyn_cast<ConstantInt>(Op1);

                        if (C0 && C0->isOne()) {
                            // Forma  1 * x : il risultato e' x (= Op1).
                            Op->replaceAllUsesWith(Op1);
                            Op->eraseFromParent();
                            Changed = true;
                        } else if (C1 && C1->isOne()) {
                            // Forma  x * 1 : il risultato e' x (= Op0).
                            Op->replaceAllUsesWith(Op0);
                            Op->eraseFromParent();
                            Changed = true;
                        }
                    }
                }
            }
        }

        // Valore di ritorno: comunica a LLVM se abbiamo toccato l'IR.
        //   - PreservedAnalyses::none()  = "ho cambiato il codice, le analisi
        //     precedenti potrebbero non essere piu' valide, ricalcolatele"
        //   - PreservedAnalyses::all()   = "non ho cambiato niente, tutto e'
        //     ancora valido, non buttare via le analisi"
        if (Changed) {
            return PreservedAnalyses::none();
        }

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
                        if (Name == "algebric-identity") {
                            FPM.addPass(AlgebricIdentity());
                            return true; // "si', ho riconosciuto e aggiunto il passo"
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