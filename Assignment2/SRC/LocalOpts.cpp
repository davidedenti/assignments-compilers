//===----------------------------------------------------------------------===//
//  LocalOpts.cpp  —  SECONDO Assignment di "Compilatori - Middle end"
//                    Dataflow Analysis  ->  problema 1: VERY BUSY EXPRESSIONS
//
//
//  ----------------------------------------------------------------------------
//
//  Un'espressione (es. a+b) e' "very busy" in un punto p se, QUALUNQUE cammino
//  si prenda da p fino a EXIT, quell'espressione viene sempre calcolata prima
//  che uno dei suoi operandi venga ridefinito. Sapere questo permette il "code
//  hoisting" (spostare il calcolo piu' in alto una volta sola).
//
//  Parametri del framework di dataflow per Very Busy Expressions:
//    - Domain (dominio)      : gli insiemi di ESPRESSIONI del programma
//    - Direction (direzione) : BACKWARD (all'indietro): si parte da EXIT e si
//                              risale verso ENTRY. Per questo nel codice i
//                              blocchi vengono scorsi in ordine INVERSO.
//    - Meet operation (∧)    : INTERSEZIONE. Un'espressione e' very busy solo se
//                              lo e' su TUTTI i cammini, cioe' se compare nell'IN
//                              di TUTTI i successori.
//    - Boundary condition    : OUT[EXIT] = ∅ (insieme vuoto): dal blocco di
//                              uscita non "esce" nessuna espressione very busy.
//    - Initial interior pts. : di norma si inizializza al TOP = insieme di TUTTE
//                              le espressioni (poi l'intersezione lo restringe).
//    - Transfer function     : IN[B] = Gen[B] ∪ (OUT[B] − Kill[B])
//                              dove OUT[B] = ∧ (IN dei successori)
//                              Gen[B]  = espressioni usate in B prima che i loro
//                                        operandi vengano ridefiniti in B
//                              Kill[B] = espressioni i cui operandi vengono
//                                        ridefiniti in B
//
//  Nel codice qui sotto: la parte di ITERAZIONE e di MEET (intersezione sui
//  successori) e' abbozzata; la TRANSFER FUNCTION (Gen/Kill) e il salvataggio
//  degli stati non sono ancora collegati (vedi i TODO).
//===----------------------------------------------------------------------===//

// ---- Header di LLVM e della libreria standard ----
#include "llvm/IR/Function.h"          // Function, BasicBlock
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"       // PassInfoMixin, PreservedAnalyses (New Pass Manager)
#include "llvm/Passes/PassBuilder.h"   // per registrare il passo nella pipeline
#include "llvm/Passes/PassPlugin.h"    // per compilare tutto come plugin caricabile
#include "llvm/ADT/STLExtras.h"        // utility LLVM, tra cui llvm::reverse(...)
#include "llvm/IR/CFG.h"               // successors(), succ_empty(): navigare il CFG
#include <cstddef>
#include <functional>
#include <set>                         // std::set -> lo usiamo per gli insiemi di espressioni
#include <string>                      // simbolo dell'operatore di Operation
#include <vector>
#include <unordered_map>               // mappa BasicBlock* -> indice



using namespace llvm; // cosi' non dobbiamo scrivere "llvm::" davanti a tutto

struct Operation {
    Value *Operando1;
    Value *Operando2;
    std::string Operatore;

    bool operator<(const Operation &Altra) const {
        if (Operatore != Altra.Operatore) {
            return Operatore < Altra.Operatore;
        }

        Value *PrimoOperando = Operando1;
        Value *SecondoOperando = Operando2;
        Value *AltroPrimoOperando = Altra.Operando1;
        Value *AltroSecondoOperando = Altra.Operando2;
        std::less<Value *> Confronta;

        if (Operatore == "+" || Operatore == "*") {
            if (Confronta(SecondoOperando, PrimoOperando)) {
                PrimoOperando = Operando2;
                SecondoOperando = Operando1;
            }
            if (Confronta(AltroSecondoOperando, AltroPrimoOperando)) {
                AltroPrimoOperando = Altra.Operando2;
                AltroSecondoOperando = Altra.Operando1;
            }
        }

        if (PrimoOperando != AltroPrimoOperando) {
            return Confronta(PrimoOperando, AltroPrimoOperando);
        }
        return Confronta(SecondoOperando, AltroSecondoOperando);
    }

    bool operator==(const Operation &Altra) const {
        return !(*this < Altra) && !(Altra < *this);
    }
};

// Stato di dataflow di UN singolo basic block: i due insiemi IN e OUT.
//   - In  = espressioni very busy all'INIZIO del blocco
//   - Out = espressioni very busy alla FINE del blocco
struct StatoDataFlow {
    std::set<Operation> In;
    std::set<Operation> Out;
};




// Confronta lo stato di TUTTI i blocchi tra l'iterazione precedente (prev) e
// quella corrente (curr). Serve come CONDIZIONE DI TERMINAZIONE dell'algoritmo
// iterativo: quando In e Out non cambiano piu' per nessun blocco, abbiamo
// raggiunto il "punto fisso" e possiamo fermarci.
// Ritorna true se ALMENO un blocco e' cambiato.
bool isChanged(std::vector<StatoDataFlow> prev, std::vector<StatoDataFlow> curr) {
    for (int i = 0; i < prev.size(); i++) {
        if (prev.at(i).In != curr.at(i).In) return true;   // e' cambiato l'IN?
        if (prev.at(i).Out != curr.at(i).Out) return true; // e' cambiato l'OUT?
    }

    return false; // nessuna differenza -> punto fisso raggiunto
}

std::string getOperation(const BinaryOperator &Op) {
    if (Op.getOpcode() == Instruction::Add) { return "+"; }
    if (Op.getOpcode() == Instruction::Sub) { return "-"; }
    if (Op.getOpcode() == Instruction::Mul) { return "*"; }
    if (Op.getOpcode() == Instruction::SDiv) { return "/"; }

    return "";
}

Value *getOperando(Value *Operando) {
    LoadInst *Load = dyn_cast<LoadInst>(Operando);

    if (Load) {
        return Load->getPointerOperand();
    }

    return Operando;
}

bool checkKill(const std::vector<Value *> &VariabiliModificate, Value *Operando1, Value *Operando2) {
    for (int i = 0; i < VariabiliModificate.size(); i++) {
        if (Operando1 == VariabiliModificate.at(i) || Operando2 == VariabiliModificate.at(i)) {
            return true;
        }
    }

    return false;
}

bool isInGenerate(const std::vector<Operation> &EspressioniGenerate, const BinaryOperator &Op) {
    std::string Operatore = getOperation(Op);
    Value *Operando1 = getOperando(Op.getOperand(0));
    Value *Operando2 = getOperando(Op.getOperand(1));

    for (const Operation &I : EspressioniGenerate) {
        if (I.Operatore != Operatore) { continue; }

        if (I.Operatore == "+" || I.Operatore == "*") {
            if ((Operando1 == I.Operando1 && Operando2 == I.Operando2) || (Operando1 == I.Operando2 && Operando2 == I.Operando1)) {
                return true;
            }
        } else {
            if (Operando1 == I.Operando1 && Operando2 == I.Operando2) {
                return true;
            }
        }
    }

    return false;
}


std::set<Operation> calcolaGenKill(BasicBlock &BB, const std::set<Operation> &Out) {
    std::vector<Value *> VariabiliModificate;
    std::vector<Operation> EspressioniGenerate;

    for (Instruction &I : BB) {
        StoreInst *Store = dyn_cast<StoreInst>(&I);

        if (Store) {
            Value *VariabileModificata = Store->getPointerOperand();

            VariabiliModificate.push_back(VariabileModificata);
        }

        BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

        if (Op && !getOperation(*Op).empty() && !checkKill(VariabiliModificate, getOperando(Op->getOperand(0)), getOperando(Op->getOperand(1)))) {
            Value *OperandoSinistro = getOperando(Op->getOperand(0));
            Value *OperandoDestro = getOperando(Op->getOperand(1));
            Value *Risultato = Op;

            if (!isInGenerate(EspressioniGenerate, *Op)) {
                Operation nuovaGenerata;
                nuovaGenerata.Operando1 = OperandoSinistro;
                nuovaGenerata.Operando2 = OperandoDestro;
                nuovaGenerata.Operatore = getOperation(*Op);
                EspressioniGenerate.push_back(nuovaGenerata);
            }
        }
    }

    std::set<Operation> In = Out;

    for (const Operation &Espressione : Out) {
        if (checkKill(VariabiliModificate, Espressione.Operando1, Espressione.Operando2)) {
            In.erase(Espressione);
        }
    }

    In.insert(EspressioniGenerate.begin(), EspressioniGenerate.end());

    return In;
}



// Il passo vero e proprio. "PassInfoMixin<...>" e' boilerplate del New Pass
// Manager; la logica sta tutta in run(), chiamata una volta per Function.
struct VeryBusyExpressions : PassInfoMixin<VeryBusyExpressions> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;       // c'e' stato un cambiamento in quest'iterazione?
        int IterationsCounter = 0;  // numero dell'iterazione corrente
        // "Iterazioni" e' pensato per conservare, per OGNI iterazione, lo stato
        // (In/Out) di TUTTI i blocchi: e' la tabella delle iterazioni chiesta
        // dalla traccia (Iterazione 1, 2, 3, ...).
        std::vector<std::vector<StatoDataFlow>> Iterazioni;

        // Mappa che associa a ogni basic block un indice numerico, cosi'
        // possiamo ritrovare il suo stato dentro i vettori per posizione.
        std::unordered_map<BasicBlock *, int> BlockIndex;
        std::set<Operation> TutteLeEspressioni;
        int Index = 0;

        for (BasicBlock &BB : F) {
            BlockIndex[&BB] = Index;

            for (Instruction &I : BB) {
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (Op && !getOperation(*Op).empty()) {
                    Operation Espressione;
                    Espressione.Operando1 = getOperando(Op->getOperand(0));
                    Espressione.Operando2 = getOperando(Op->getOperand(1));
                    Espressione.Operatore = getOperation(*Op);
                    TutteLeEspressioni.insert(Espressione);
                }
            }

            Index++;
        }

        std::vector<StatoDataFlow> StatoIniziale(Index);

        for (BasicBlock &BB : F) {
            StatoIniziale.at(BlockIndex.at(&BB)).In = TutteLeEspressioni;
            StatoIniziale.at(BlockIndex.at(&BB)).Out = TutteLeEspressioni;

            if (succ_empty(&BB)) {
                StatoIniziale.at(BlockIndex.at(&BB)).Out = {};
            }
        }

        Iterazioni.push_back(StatoIniziale);

        // Ciclo iterativo classico dell'analisi di dataflow: si ripete finche'
        // lo stato continua a cambiare (finche' non si raggiunge il punto fisso).
        do{
            std::vector<StatoDataFlow> StatoCorrente(Index);

            // Scorriamo i blocchi in ordine INVERSO: coerente con un'analisi
            // BACKWARD (partiamo dai blocchi vicini a EXIT e risaliamo).
            for (BasicBlock &BB : llvm::reverse(F)) {
                StatoDataFlow Data;          // stato che stiamo costruendo per questo blocco

                // --- BOUNDARY CONDITION ---
                // Se il blocco non ha successori, e' un blocco di uscita (EXIT):
                // il suo OUT parte vuoto.
                if (succ_empty(&BB)) {
                    Data.Out = {};
                }
                else {
                    // --- MEET OPERATION: intersezione sugli IN dei successori ---
                    // OUT[B] = intersezione degli IN di tutti i successori di B.
                    // (Un'espressione sopravvive solo se e' presente in TUTTI.)
                    std::set<Operation> Intersection;
                    bool PrimoSuccessore = true;

                    for (BasicBlock *Successore : successors(&BB)) {
                        // Prendiamo l'insieme IN del successore, come calcolato
                        // nell'iterazione precedente (letto da "Iterazioni").
                        const std::set<Operation> &InSuccessore = Iterazioni.at(IterationsCounter).at(BlockIndex.at(Successore)).In;

                        if (PrimoSuccessore) {
                            // Primo successore: l'intersezione parte da questo insieme.
                            Intersection = InSuccessore;
                            PrimoSuccessore = false;
                        } else {
                            // Successori seguenti: teniamo solo le espressioni che
                            // sono ANCHE in questo successore (intersezione manuale).
                            std::set<Operation> NuovaIntersection;

                            for (const Operation &Espressione : Intersection) {
                                // find(...) != end()  significa "l'elemento c'e'".
                                if (InSuccessore.find(Espressione) != InSuccessore.end()) {
                                    NuovaIntersection.insert(Espressione);
                                }
                            }

                            Intersection = NuovaIntersection;
                        }
                    }

                    // Il risultato dell'intersezione viene salvato (per ora) in Data.In.
                    // TODO: qui dovra' inserirsi la TRANSFER FUNCTION
                    //       IN[B] = Gen[B] ∪ (OUT[B] − Kill[B]), scorrendo le
                    //       istruzioni del blocco per calcolare Gen e Kill.
                    Data.Out = Intersection;
                }

                Data.In = calcolaGenKill(BB, Data.Out);
                StatoCorrente.at(BlockIndex.at(&BB)) = Data;

                // TODO: 'Data' e 'Index' non vengono ancora usati dopo:
                //   - manca il salvataggio di 'Data' nel vettore dell'iterazione
                //     corrente (per costruire la tabella e permettere all'iterazione
                //     successiva di leggere gli IN aggiornati);
                //   - 'Index' non viene incrementato.
            }

            Iterazioni.push_back(StatoCorrente);

            // --- CONDIZIONE DI TERMINAZIONE ---
            // Prima iterazione utile: forziamo Changed a true per continuare.
            // Dalle successive: confrontiamo lo stato corrente col precedente
            // tramite isChanged() (punto fisso quando non cambia piu' nulla).
            if (IterationsCounter == 0) Changed = true;
            else Changed = isChanged(Iterazioni.at(IterationsCounter), Iterazioni.at(IterationsCounter + 1));

            IterationsCounter++; // passiamo all'iterazione successiva
        }while (Changed);


        // Quest'analisi non modifica l'IR (solo lo studia), quindi diciamo a
        // LLVM che tutte le analisi restano valide.
        return PreservedAnalyses::all();
    }
};

//===----------------------------------------------------------------------===//
//  REGISTRAZIONE DEL PLUGIN
//  Presenta il passo a LLVM: si potra' invocare da riga di comando con
//      opt -load-pass-plugin=./LocalOpts.so -passes=very-busy-expressions ...
//===----------------------------------------------------------------------===//
PassPluginLibraryInfo getTestPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, // versione dell'API dei plugin
        "LocalOpts",             // nome del plugin
        LLVM_VERSION_STRING,     // versione di LLVM con cui e' compilato
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback([](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
                // Se il nome dopo -passes= e' il nostro, aggiungiamo il passo.
                if (Name == "very-busy-expressions") {
                    FPM.addPass(VeryBusyExpressions());
                    return true;
                }

                return false; // nome non nostro
            });
        }
    };
}

// Punto d'ingresso che LLVM cerca quando carica il plugin (.so).
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}