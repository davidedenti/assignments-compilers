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
//  FUNZIONI DI SUPPORTO PER LA STRENGTH REDUCTION
//
//  "Strength reduction" = sostituire un'operazione COSTOSA con una o piu'
//  operazioni ECONOMICHE che danno lo stesso risultato. Esempi tipici:
//      x * 8   ->  x << 3         (moltiplicare per una potenza di 2 = shift)
//      x * 15  ->  (x << 4) - x   (perche' 15 = 16 - 1)
//      x / 8   ->  x >> 3         (dividere per una potenza di 2 = shift a destra)
//
//  Per decidere SE conviene, usiamo un semplice "modello di costo": diamo un
//  costo indicativo a ciascuna operazione e trasformiamo solo se il risultato
//  costa meno dell'originale.
//===----------------------------------------------------------------------===//

// Costo (indicativo, in "unita'") di ogni tipo di istruzione.
// Sono numeri scelti a mano: mul e' cara, div e' carissima, shift/add/sub sono economici.
static unsigned getInstructionCost(unsigned Opcode) {
    if (Opcode == Instruction::Mul) { return 3; }
    if (Opcode == Instruction::UDiv) { return 10; }
    if (Opcode == Instruction::Shl || Opcode == Instruction::LShr) { return 1; }
    if (Opcode == Instruction::Add || Opcode == Instruction::Sub) { return 1; }
    return 1; // default per tutto il resto
}

// "Conviene?" -> Si', se il costo della sostituzione e' MINORE del costo
// dell'operazione originale.
static bool isProfitable(unsigned OriginalOpcode, unsigned ReplacementCost) {
    return ReplacementCost < getInstructionCost(OriginalOpcode);
}

// Calcola QUANTO costerebbe rimpiazzare  x * Constant  con shift/add/sub,
// SENZA ancora creare le istruzioni (serve solo per decidere se conviene).
//
// "APInt" e' il tipo di LLVM per interi di larghezza arbitraria (Arbitrary
// Precision Integer): lo usiamo perche' le costanti possono essere a 8, 16,
// 32, 64... bit.
static unsigned getMultiplyCost(const APInt &Constant) {
    APInt Magnitude = Constant.abs(); // lavoriamo sul valore assoluto; il segno lo gestiamo dopo
    unsigned Cost = 0;
    unsigned Terms = 0; // quanti "pezzi" (termini) stiamo sommando

    if (Magnitude.isOne()) {
        // x * 1 = x : nessuna operazione necessaria.
        Cost = 0;
    } else if (Magnitude.isPowerOf2()) {
        // x * 2^k = x << k : basta un solo shift.
        Cost = getInstructionCost(Instruction::Shl);
    } else if ((Magnitude + 1).isPowerOf2()) {
        // Costante del tipo  2^k - 1  (es. 3, 7, 15, 31...).
        // x * (2^k - 1) = (x << k) - x : uno shift + una sottrazione.
        Cost = getInstructionCost(Instruction::Shl) + getInstructionCost(Instruction::Sub);
    } else {
        // Caso generale: scomponiamo la costante nei suoi bit a 1.
        // Ogni bit a 1 in posizione "Bit" corrisponde a un termine  x << Bit,
        // e i termini vanno sommati tra loro.
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; } // saltiamo i bit a 0

            // Il bit 0 non richiede shift (x << 0 = x); gli altri si'.
            if (Bit != 0) { Cost += getInstructionCost(Instruction::Shl); }
            // Dal secondo termine in poi serve una addizione per sommarlo ai precedenti.
            if (Terms != 0) { Cost += getInstructionCost(Instruction::Add); }

            Terms++;
        }
    }

    // Se la costante era negativa, alla fine serve una sottrazione extra (0 - risultato)
    // per cambiare segno.
    if (Constant.isNegative()) { Cost += getInstructionCost(Instruction::Sub); }

    return Cost;
}

// Costruisce DAVVERO le istruzioni che calcolano  ValueToMultiply * Constant
// usando solo shift/add/sub. Rispecchia esattamente la logica di getMultiplyCost.
//
// "IRBuilder" e' un aiutante che crea nuove istruzioni e le inserisce nel punto
// giusto. Ogni Create... (CreateShl, CreateAdd, CreateSub...) genera l'istruzione
// corrispondente e restituisce il Value con il suo risultato.
static Value *createMultiplyByConstant(IRBuilder<> &Builder, Value *ValueToMultiply, const APInt &Constant) {
    APInt Magnitude = Constant.abs();
    Value *Result = nullptr;

    if (Magnitude.isOne()) {
        // x * 1 = x
        Result = ValueToMultiply;
    } else if (Magnitude.isPowerOf2()) {
        // x * 2^k = x << k. logBase2() ci da' l'esponente k.
        Result = Builder.CreateShl(ValueToMultiply, Magnitude.logBase2(), "strength.shift");
    } else if ((Magnitude + 1).isPowerOf2()) {
        // x * (2^k - 1) = (x << k) - x
        Value *ShiftedValue = Builder.CreateShl(ValueToMultiply, (Magnitude + 1).logBase2(), "strength.shift");
        Result = Builder.CreateSub(ShiftedValue, ValueToMultiply, "strength.sub");
    } else {
        // Caso generale: somma di  x << Bit  per ogni bit a 1 della costante.
        for (unsigned Bit = 0; Bit < Magnitude.getBitWidth(); Bit++) {
            if (!Magnitude[Bit]) { continue; }

            Value *Term = ValueToMultiply;

            // Se il bit non e' quello di posizione 0, il termine e' x << Bit.
            if (Bit != 0) {
                Term = Builder.CreateShl(ValueToMultiply, Bit, "strength.shift");
            }

            if (!Result) {
                // Primo termine: diventa il risultato iniziale.
                Result = Term;
            } else {
                // Termini successivi: si sommano a quanto accumulato finora.
                Result = Builder.CreateAdd(Result, Term, "strength.add");
            }
        }
    }

    // Gestione del segno: se la costante era negativa, calcoliamo  0 - Result.
    if (Constant.isNegative()) {
        Result = Builder.CreateSub(ConstantInt::get(ValueToMultiply->getType(), 0), Result, "strength.neg");
    }

    return Result;
}

// Calcola la "meta' alta" del prodotto  Dividend * Magic  (in inglese MULHU =
// MULtiply High Unsigned). Serve per la divisione con numeri magici (vedi sotto).
//
// L'idea: se moltiplico due numeri a N bit, il risultato ha fino a 2N bit.
// A noi interessano i bit ALTI. Quindi:
//    1) allarghiamo tutto al doppio della larghezza (N -> 2N bit) con ZExt
//       (zero-extend: aggiunge zeri davanti, corretto perche' lavoriamo unsigned)
//    2) facciamo la moltiplicazione a 2N bit
//    3) prendiamo i bit alti con uno shift a destra di N
//    4) ritagliamo (Trunc) di nuovo a N bit
static Value *createUnsignedMultiplyHigh(IRBuilder<> &Builder, Value *Dividend, const APInt &Magic) {
    IntegerType *Type = dyn_cast<IntegerType>(Dividend->getType());
    unsigned BitWidth = Type->getBitWidth();                       // N
    IntegerType *WideType = IntegerType::get(Builder.getContext(), BitWidth * 2); // 2N bit

    Value *WideDividend = Builder.CreateZExt(Dividend, WideType, "magic.dividend"); // allarga il dividendo
    Value *WideMagic = ConstantInt::get(WideType, Magic.zext(BitWidth * 2));        // allarga il numero magico
    Value *Product = Builder.CreateMul(WideDividend, WideMagic, "magic.product");   // prodotto a 2N bit
    Value *HighHalf = Builder.CreateLShr(Product, BitWidth, "magic.high");          // tieni i bit alti

    return Builder.CreateTrunc(HighHalf, Type, "magic.quotient");                   // torna a N bit
}

// Costo stimato della divisione tramite numero magico. Serve solo a decidere
// se conviene rispetto a una UDiv (che costa 10). "Magic" contiene i parametri
// calcolati da LLVM (numero magico, shift, e un flag "IsAdd" che indica se serve
// un passaggio di correzione in piu').
static unsigned getMagicDivisionCost(const UnsignedDivisionByConstantInfo &Magic) {
    // Costo base: una moltiplicazione (alta) + uno shift.
    unsigned Cost = getInstructionCost(Instruction::Mul) + getInstructionCost(Instruction::LShr);

    // Alcuni divisori richiedono uno shift PRIMA della moltiplicazione (pre-shift).
    if (Magic.PreShift != 0) { Cost += getInstructionCost(Instruction::LShr); }

    // Il ramo "IsAdd" e' una correzione che serve per certi divisori: costa
    // una sub + uno shift + una add in piu'.
    if (Magic.IsAdd) {
        Cost += getInstructionCost(Instruction::Sub);
        Cost += getInstructionCost(Instruction::LShr);
        Cost += getInstructionCost(Instruction::Add);
    }

    // E infine un eventuale shift DOPO (post-shift).
    if (Magic.PostShift != 0) { Cost += getInstructionCost(Instruction::LShr); }

    return Cost;
}


//===----------------------------------------------------------------------===//
//  PASSO 3 - StrengthReduction
//
//  Gestisce due tipi di istruzione:
//    A) Mul per costante  -> shift/add/sub  (usando le funzioni di supporto)
//    B) UDiv per costante -> shift (se potenza di 2) oppure numeri magici
//===----------------------------------------------------------------------===//
struct StrengthReduction : PassInfoMixin<StrengthReduction> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        bool Changed = false;

        for (BasicBlock &B : F) {
            for (auto It = B.begin(); It != B.end();) {
                Instruction &I = *It++;
                BinaryOperator *Op = dyn_cast<BinaryOperator>(&I);

                if (!Op) { continue; } // ci interessano solo le operazioni binarie

                //================= A) MOLTIPLICAZIONE per costante =================
                if (Op->getOpcode() == Instruction::Mul) {
                    // Come nel passo 2, per prudenza non tocchiamo le mul con flag
                    // nsw/nuw (evitiamo di ragionare sul poison in caso di overflow).
                    if (Op->hasNoSignedWrap() || Op->hasNoUnsignedWrap()) { continue; }

                    // Individuiamo quale dei due operandi e' la costante.
                    ConstantInt *C0 = dyn_cast<ConstantInt>(Op->getOperand(0));
                    ConstantInt *C1 = dyn_cast<ConstantInt>(Op->getOperand(1));
                    ConstantInt *Constant = nullptr;   // la costante moltiplicativa
                    Value *OriginalValue = nullptr;    // l'altro operando (x)

                    if (C0) {
                        Constant = C0;
                        OriginalValue = Op->getOperand(1);
                    } else if (C1) {
                        Constant = C1;
                        OriginalValue = Op->getOperand(0);
                    }

                    // Se non c'e' costante, o la costante e' 0, non facciamo strength
                    // reduction (x * 0 e' un altro tipo di semplificazione, non nostra).
                    if (!Constant || Constant->isZero()) { continue; }

                    // Quanto costerebbe la versione con shift/add/sub?
                    unsigned ReplacementCost = getMultiplyCost(Constant->getValue());

                    // Trasformiamo solo se conviene davvero (costo < costo della mul).
                    if (!isProfitable(Instruction::Mul, ReplacementCost)) { continue; }

                    // IRBuilder posizionato su Op: le nuove istruzioni verranno
                    // inserite PRIMA di Op.
                    IRBuilder<> Builder(Op);
                    Value *Replacement = createMultiplyByConstant(Builder, OriginalValue, Constant->getValue());

                    // Sostituiamo la mul con la nuova sequenza e la cancelliamo.
                    Op->replaceAllUsesWith(Replacement);
                    Op->eraseFromParent();
                    Changed = true;
                    continue;
                }

                //================= B) DIVISIONE (unsigned) per costante =================
                // Se non e' una UDiv, saltiamo.
                if (Op->getOpcode() != Instruction::UDiv) { continue; }
                // "exact" significa che la divisione e' promessa senza resto; e' un
                // altro caso particolare che, per prudenza, non tocchiamo.
                if (Op->isExact()) { continue; }

                Value *Dividend = Op->getOperand(0); // il numero da dividere
                ConstantInt *Divisor = dyn_cast<ConstantInt>(Op->getOperand(1)); // il divisore

                // Il divisore deve essere una costante, e non 0 ne' 1
                // (dividere per 1 = x, dividere per 0 e' indefinito).
                if (!Divisor || Divisor->isZero() || Divisor->isOne()) { continue; }

                const APInt &DivisorValue = Divisor->getValue();
                IRBuilder<> Builder(Op);

                // ---- Caso semplice: divisore potenza di 2 ----
                //   x / 2^k  =  x >> k     (shift logico a destra)
                if (DivisorValue.isPowerOf2()) {
                    unsigned Shift = DivisorValue.logBase2(); // k

                    // Conviene? (uno shift costa 1, la div costa 10: si').
                    if (!isProfitable(Instruction::UDiv, getInstructionCost(Instruction::LShr))) { continue; }

                    Value *Replacement = Builder.CreateLShr(Dividend, Shift, "strength.lshr");

                    Op->replaceAllUsesWith(Replacement);
                    Op->eraseFromParent();
                    Changed = true;
                    continue;
                }

                // ---- Caso generale: divisore NON potenza di 2 -> "numeri magici" ----
                //
                // IDEA: dividere per una costante D si puo' fare senza usare la
                // divisione hardware, moltiplicando per un numero "magico" M
                // (precalcolato) e prendendo i bit alti del prodotto, con qualche
                // aggiustamento (pre-shift, correzione "add", post-shift).
                // LLVM ci calcola M e i parametri con UnsignedDivisionByConstantInfo.
                UnsignedDivisionByConstantInfo Magic = UnsignedDivisionByConstantInfo::get(DivisorValue);
                unsigned ReplacementCost = getMagicDivisionCost(Magic);

                // Trasformiamo solo se il totale conviene rispetto alla UDiv.
                if (!isProfitable(Instruction::UDiv, ReplacementCost)) { continue; }

                Value *AdjustedDividend = Dividend;

                // (1) Eventuale shift PRIMA (pre-shift), usato per certi divisori pari.
                if (Magic.PreShift != 0) {
                    AdjustedDividend = Builder.CreateLShr(AdjustedDividend, Magic.PreShift, "magic.preshift");
                }

                // (2) Moltiplicazione "alta" per il numero magico -> quoziente approssimato.
                Value *Quotient = createUnsignedMultiplyHigh(Builder, AdjustedDividend, Magic.Magic);

                // (3) Correzione "add" (serve per alcuni divisori, quando il numero
                //     magico non entra tutto nei bit disponibili):
                //         Quotient = Quotient + ((Dividend - Quotient) >> 1)
                if (Magic.IsAdd) {
                    Value *Difference = Builder.CreateSub(AdjustedDividend, Quotient, "magic.difference");
                    Value *HalfDifference = Builder.CreateLShr(Difference, 1, "magic.half");
                    Quotient = Builder.CreateAdd(Quotient, HalfDifference, "magic.adjusted");
                }

                // (4) Eventuale shift DOPO (post-shift) per aggiustare la scala finale.
                if (Magic.PostShift != 0) {
                    Quotient = Builder.CreateLShr(Quotient, Magic.PostShift, "magic.postshift");
                }

                // Sostituiamo la divisione con la sequenza appena costruita.
                Op->replaceAllUsesWith(Quotient);
                Op->eraseFromParent();
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
                        if (Name == "algebric-identity") {
                            FPM.addPass(AlgebricIdentity());
                            return true; // "si', ho riconosciuto e aggiunto il passo"
                        }
                        if (Name == "multi-instruction-optimization") {
                            FPM.addPass(MultiInstructionOptimization());
                            return true;
                        }
                        if (Name == "strength-reduction") {
                            FPM.addPass(StrengthReduction());
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