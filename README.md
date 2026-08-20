# Compilatori – Middle end · Assignments

Passi LLVM sviluppati per il corso **Compilatori – Middle end** [I215-014]
(UNIMORE, Prof. Andrea Marongiu, a.a. 2025/2026).

I passi sono realizzati come plugin **out-of-tree** per il **New Pass Manager**
di **LLVM 19.1**, caricati a runtime con `opt -load-pass-plugin`.

## Struttura della repo

```
assignments-compilers/
├── Assignment1/
│   ├── SRC/        # sorgenti dei passi + CMakeLists.txt
│   ├── BUILD/      # output di compilazione (generato, non versionato)
│   └── TEST/       # test in C + README dedicato
├── Assignment3/
│   ├── SRC/
│   ├── BUILD/
│   └── TEST/
├── Assignment4/
│   ├── SRC/
│   ├── BUILD/
│   └── TEST/
└── ...             # Assignment2
```

Ogni assignment è autonomo e segue la stessa procedura di build: cambia solo il
contenuto di `SRC/`.

## Requisiti

- LLVM 19.1 con header di sviluppo
- CMake ≥ 3.20
- un compilatore C++17
- clang 19 per generare l'IR dei test (vedi il `README.md` di `TEST/`)

Controlla di usare la versione giusta della toolchain:

```bash
opt --version      # deve riportare 19.x
clang --version    # deve riportare 19.x
```

Se sul sistema convivono più versioni di LLVM, imposta la 19 come default con
`update-alternatives` oppure metti `/usr/lib/llvm-19/bin` in testa al `PATH`.

## Build

Dalla cartella dell'assignment (es. `Assignment1/` o `Assignment3/`):

```bash
mkdir -p BUILD && cd BUILD
cmake ../SRC
make
```

I plugin `.so` vengono prodotti nella cartella `BUILD/`. Numero e nomi dipendono
dall'assignment: vedi la sezione **Assignment implementati** più sotto.

> Se CMake non trova LLVM, indicagli esplicitamente l'installazione 19:
> ```bash
> cmake -DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm ../SRC
> ```

## Eseguire un passo

```bash
opt -load-pass-plugin=./BUILD/<Plugin>.so -passes=<nome-passo> -S input.ll -o output.ll
```

Per generare gli `input.ll` dai test in C e per gli esempi pronti all'uso, vedi
il **`README.md`** dentro la cartella `TEST/` di ciascun assignment.

## Assignment implementati

### Assignment 1 — Local Optimizations

Un plugin separato per passo.

| Ottimizzazione                 | Plugin (`.so`)                     | Nome del passo                    |
| ------------------------------ | ---------------------------------- | --------------------------------- |
| Algebraic Identity             | `AlgebricIdentity.so`              | `algebric-identity`               |
| Multi-Instruction Optimization | `MultiInstructionOptimization.so`  | `multi-instruction-optimization`  |
| Strength Reduction             | `StrengthReduction.so`             | `strength-reduction`              |

Test: `Assignment1/TEST/README.md`.

### Assignment 3 — Loop-Invariant Code Motion

Un singolo passo/plugin. L'acronimo LICM **non** è usato come nome del passo per
non entrare in conflitto con il passo ufficiale di LLVM.

| Ottimizzazione            | Plugin (`.so`)             | Nome del passo          |
| ------------------------- | -------------------------- | ----------------------- |
| Loop-Invariant Code Motion | `LoopInvariantMotion.so`  | `loop-invariant-motion` |

Il passo usa le analisi **LoopInfo** e **DominatorTree** e richiede la forma
canonica dei loop: prima del passo va eseguito `mem2reg,loop-simplify` (il passo
esce senza fare nulla se il loop non ha un preheader).

Test: `Assignment3/TEST/README.md`.

### Assignment 4 — Loop Fusion

Un singolo passo/plugin. Il nome ufficiale LLVM `loop-fusion` è evitato per non
entrare in conflitto.

| Ottimizzazione | Plugin (`.so`)   | Nome del passo      |
| -------------- | ---------------- | ------------------- |
| Loop Fusion    | `LoopFusion.so`  | `loop-fusion-pass`  |

Il passo è un FunctionPass e usa le analisi **LoopInfo**, **DominatorTree**,
**PostDominatorTree** e **ScalarEvolution**. Verifica le quattro condizioni di
fusione (adiacenza, stesso trip count, control-flow equivalence, assenza di
dipendenze a distanza negativa, queste ultime via SCEV) e poi fonde. La pipeline
di preparazione è `mem2reg,loop-simplify`.

> **Ambito.** Il passo gestisce sia loop **non guarded (top-tested)** sia loop
> **guarded (ruotati)**: sceglie la strada in base a `getLoopGuardBranch()`.
> L'induction variable è recuperata con `getInductionVariable` (forma ruotata) e,
> in fallback, `getCanonicalInductionVariable` (forma top-tested). Fonde una
> coppia per esecuzione del passo: per collassare più loop adiacenti si rilancia
> il passo. I test usano la forma top-tested (`mem2reg,loop-simplify`, senza
> `loop-rotate`).

Test: `Assignment4/TEST/README.md`.