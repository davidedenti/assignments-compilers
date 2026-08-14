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
#include "llvm/IR/PassManager.h"       // PassInfoMixin, PreservedAnalyses (New Pass Manager)
#include "llvm/Passes/PassBuilder.h"   // per registrare il passo nella pipeline
#include "llvm/Passes/PassPlugin.h"    // per compilare tutto come plugin caricabile
#include "llvm/ADT/STLExtras.h"        // utility LLVM, tra cui llvm::reverse(...)
#include "llvm/IR/CFG.h"               // successors(), succ_empty(): navigare il CFG
#include <cstddef>
#include <set>                         // std::set -> lo usiamo per gli insiemi di espressioni
#include <string>                      // rappresentiamo ogni espressione come stringa
#include <vector>
#include <unordered_map>               // mappa BasicBlock* -> indice



using namespace llvm; // cosi' non dobbiamo scrivere "llvm::" davanti a tutto

// Un'espressione la rappresentiamo come stringa (es. "a+b"); un insieme di
// espressioni e' quindi un set di stringhe. Usare std::set ci da' gratis
// l'ordinamento e le operazioni insiemistiche.
using ExpressionSet = std::set<std::string>;

// Stato di dataflow di UN singolo basic block: i due insiemi IN e OUT.
//   - In  = espressioni very busy all'INIZIO del blocco
//   - Out = espressioni very busy alla FINE del blocco
struct StatoDataFlow {
    ExpressionSet In;
    ExpressionSet Out;
};




// Confronta lo stato di TUTTI i blocchi tra l'iterazione precedente (prev) e
// quella corrente (curr). Serve come CONDIZIONE DI TERMINAZIONE dell'algoritmo
// iterativo: quando In e Out non cambiano piu' per nessun blocco, abbiamo
// raggiunto il "punto fisso" e possiamo fermarci.
// Ritorna true se ALMENO un blocco e' cambiato.
bool isChanged(std::vector <StatoDataFlow> prev, std::vector <StatoDataFlow> curr) {

    for (int i = 0; i < prev.size(); i++) {
        if (prev.at(i).In != curr.at(i).In) return true;   // e' cambiato l'IN?
        if (prev.at(i).Out != curr.at(i).Out) return true; // e' cambiato l'OUT?

    }
    return false; // nessuna differenza -> punto fisso raggiunto

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

        // Ciclo iterativo classico dell'analisi di dataflow: si ripete finche'
        // lo stato continua a cambiare (finche' non si raggiunge il punto fisso).
        do{

            // Mappa che associa a ogni basic block un indice numerico, cosi'
            // possiamo ritrovare il suo stato dentro i vettori per posizione.
            std::unordered_map<BasicBlock *, int> BlockIndex;
            int Index = 0;

            // Scorriamo i blocchi in ordine INVERSO: coerente con un'analisi
            // BACKWARD (partiamo dai blocchi vicini a EXIT e risaliamo).
            for (BasicBlock &BB : llvm::reverse(F)) {
                StatoDataFlow Data;          // stato che stiamo costruendo per questo blocco
                BlockIndex[&BB] = Index;     // registriamo la posizione del blocco

                // --- BOUNDARY CONDITION ---
                // Se il blocco non ha successori, e' un blocco di uscita (EXIT):
                // il suo OUT parte vuoto.
                if (succ_empty(&BB)) {
                    Data.Out = nullptr; // TODO: segnaposto -> qui va l'insieme VUOTO (OUT[EXIT] = ∅)
                }
                else {
                    // --- MEET OPERATION: intersezione sugli IN dei successori ---
                    // OUT[B] = intersezione degli IN di tutti i successori di B.
                    // (Un'espressione sopravvive solo se e' presente in TUTTI.)
                    ExpressionSet Intersection;
                    bool PrimoSuccessore = true;

                    for (BasicBlock *Successore : successors(&BB)) {
                        // Prendiamo l'insieme IN del successore, come calcolato
                        // nell'iterazione precedente (letto da "Iterazioni").
                        const ExpressionSet &InSuccessore =
                            Iterazioni.at(IterationsCounter).at(BlockIndex.at(Successore)).In;

                        if (PrimoSuccessore) {
                            // Primo successore: l'intersezione parte da questo insieme.
                            Intersection = InSuccessore;
                            PrimoSuccessore = false;
                        } else {
                            // Successori seguenti: teniamo solo le espressioni che
                            // sono ANCHE in questo successore (intersezione manuale).
                            ExpressionSet NuovaIntersection;

                            for (const std::string &Espressione : Intersection) {
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
                    Data.In = Intersection;

                }

                // TODO: 'Data' e 'Index' non vengono ancora usati dopo:
                //   - manca il salvataggio di 'Data' nel vettore dell'iterazione
                //     corrente (per costruire la tabella e permettere all'iterazione
                //     successiva di leggere gli IN aggiornati);
                //   - 'Index' non viene incrementato.

            }

            // --- CONDIZIONE DI TERMINAZIONE ---
            // Prima iterazione utile: forziamo Changed a true per continuare.
            // Dalle successive: confrontiamo lo stato corrente col precedente
            // tramite isChanged() (punto fisso quando non cambia piu' nulla).
            if (Iterazioni.size() == 1)  Changed = true;
            else Changed = isChanged(Iterazioni.at(IterationsCounter - 1),
                                     Iterazioni.at(IterationsCounter));

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
            PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        // Se il nome dopo -passes= e' il nostro, aggiungiamo il passo.
                        if (Name == "very-busy-expressions") {
                            FPM.addPass(VeryBusyExpressions());
                            return true;
                        }

                        return false; // nome non nostro
                    }
            );
        }
    };
}

// Punto d'ingresso che LLVM cerca quando carica il plugin (.so).
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getTestPassPluginInfo();
}