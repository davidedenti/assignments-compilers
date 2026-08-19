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
└── ...             # Assignment2, 3, 4 (da aggiungere)
```

Ogni assignment è autonomo e segue la stessa procedura di build: cambia solo il
contenuto di `SRC/`.

## Requisiti

- LLVM 19.1 con header di sviluppo
- CMake ≥ 3.20
- un compilatore C++17
- clang 19 per generare l'IR dei test (vedi `Assignment1/TEST/README.md`)

Controlla di usare la versione giusta della toolchain:

```bash
opt --version      # deve riportare 19.x
clang --version    # deve riportare 19.x
```

Se sul sistema convivono più versioni di LLVM, imposta la 19 come default con
`update-alternatives` oppure metti `/usr/lib/llvm-19/bin` in testa al `PATH`.

## Build

Dalla cartella dell'assignment (es. `Assignment1/`):

```bash
mkdir -p BUILD && cd BUILD
cmake ../SRC
make
```

Vengono prodotti tre plugin nella cartella `BUILD/`:

| Ottimizzazione                 | Plugin (`.so`)                     | Nome del passo                    |
| ------------------------------ | ---------------------------------- | --------------------------------- |
| Algebraic Identity             | `AlgebricIdentity.so`              | `algebric-identity`               |
| Multi-Instruction Optimization | `MultiInstructionOptimization.so`  | `multi-instruction-optimization`  |
| Strength Reduction             | `StrengthReduction.so`             | `strength-reduction`              |

> Se CMake non trova LLVM, indicagli esplicitamente l'installazione 19:
> ```bash
> cmake -DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm ../SRC
> ```

## Eseguire un passo

```bash
opt -load-pass-plugin=./BUILD/<Plugin>.so -passes=<nome-passo> -S input.ll -o output.ll
```

Per generare gli `input.ll` dai test in C e per gli esempi pronti all'uso, vedi
**`Assignment1/TEST/README.md`**.

## Assignment implementati

- **Assignment 1** — Algebraic Identity, Strength Reduction, Multi-Instruction
  Optimization. Un plugin separato per passo.
