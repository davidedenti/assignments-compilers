# Test – Assignment 4 (Loop Fusion)

Test in C per il passo `loop-fusion-pass`. I file `.c` sono i sorgenti da
versionare; gli `.ll` (ed eventuali `.dot`) sono **generati** al momento e vanno
ignorati dal `.gitignore`.

Ogni file raccoglie più funzioni, una per scenario: `opt` esegue il passo su
tutte le funzioni del modulo in un colpo solo, indipendentemente l'una
dall'altra (il passo lavora per `Function`).

Il passo fonde loop **non guarded (top-tested)** e loop **guarded (ruotati)**,
scegliendo in base a `getLoopGuardBranch()`, e trasforma **una coppia per
esecuzione** (dopo la prima fusione fa `break`, perché worklist e analisi
diventano stale). Per collassare tre loop adiacenti si rilancia il passo. I test
qui sotto usano la forma top-tested.

## File

- `loop_fusion_valid.c` — la fusione è **legale**, il passo deve stampare *«I loop possono essere fusi»* e **modificare l'IR**:
  - `fuse_indipendenti` — array diversi, nessuna dipendenza
  - `fuse_dipendenza_distanza_zero` — il 2° loop legge `a[i]` scritto dal 1° nella stessa iterazione (RAW distanza 0)
  - `fuse_tre_loop` — tre loop adiacenti a stesso trip count (in un singolo run si fonde solo la prima coppia)
- `loop_fusion_invalid.c` — la fusione è **illegale**, il passo deve stampare *«I loop non possono essere fusi»* e **lasciare l'IR invariato** (una violazione per funzione):
  - `non_adiacenti` — statement tra i due loop → viola l'adiacenza
  - `trip_count_diverso` — `N` vs `N/2` → viola lo stesso trip count
  - `non_cfg_equivalenti` — 2° loop dentro un `if` → viola la control-flow equivalence (di fatto scartato già all'adiacenza)
  - `dipendenza_negativa` — il 2° loop legge `a[i+1]` (elemento futuro) → dipendenza a distanza negativa; stampa anche *«Dipendenza backward»*

Perché array **locali** e non `int *`: due `alloca` distinti sono noti come
non-aliasing, quindi il controllo di dipendenza SCEV è preciso. Con
puntatori-parametro l'analisi dovrebbe assumere possibile aliasing e sarebbe
insufficiente. I bound sono **costanti** (`N`) e gli indici partono da 0 con
passo 1, così `ScalarEvolution` calcola trip count costante e induction variable
canonica.

## Come si genera l'IR

Due stadi. Serve `loop-simplify` (non basta `mem2reg`): il passo usa
`getLoopPreheader`, `getExitBlock`, `getLoopLatch` e la induction variable, che
richiedono la forma canonica del loop. Per questi test **non** serve
`loop-rotate`: i `for` in forma top-tested bastano (il ramo guarded del passo
gestisce comunque anche i loop ruotati, se in futuro aggiungi `loop-rotate`).

1. **`clang`** → IR grezzo (`.O0.ll`)
2. **`opt -passes='mem2reg,loop-simplify'`** → SSA + forma canonica dei loop (`.PRE.ll`)
3. **`opt -load-pass-plugin=...`** → applica il passo (`.OPT.ll`)

Flag di clang (gli stessi degli altri assignment):

- `-O0 -Xclang -disable-O0-optnone` — nessuna ottimizzazione del compilatore, ma
  senza l'attributo `optnone` che bloccherebbe il nostro passo.
- `-emit-llvm -S` — IR testuale.
- `-fno-discard-value-names` — nomi di registri e blocchi leggibili.

> Il plugin (`.so`) prodotto da `add_llvm_pass_plugin` **non** ha il prefisso
> `lib`. I comandi assumono `BUILD/` accanto a `TEST/`.

### loop_fusion_valid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_valid.c -o loop_fusion_valid.O0.ll \
 && opt -passes='mem2reg,loop-simplify' -S loop_fusion_valid.O0.ll -o loop_fusion_valid.PRE.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-pass loop_fusion_valid.PRE.ll -o loop_fusion_valid.OPT.ll
```

### loop_fusion_invalid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_invalid.c -o loop_fusion_invalid.O0.ll \
 && opt -passes='mem2reg,loop-simplify' -S loop_fusion_invalid.O0.ll -o loop_fusion_invalid.PRE.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-pass loop_fusion_invalid.PRE.ll -o loop_fusion_invalid.OPT.ll
```

## Verifica dei risultati

Lo **stdout** del terzo comando dà l'esito per ogni coppia analizzata. Atteso:

- `loop_fusion_valid.c` → tre righe `I loop possono essere fusi` (una per
  funzione; per `fuse_tre_loop` si vede una sola coppia perché dopo la fusione
  della prima coppia il passo fa `break`).
- `loop_fusion_invalid.c` → quattro righe `I loop non possono essere fusi`
  (per `dipendenza_negativa` preceduta da `Dipendenza backward`).

Poiché ora il passo trasforma davvero, confronta l'IR **prima/dopo**:

```bash
diff loop_fusion_valid.PRE.ll   loop_fusion_valid.OPT.ll     # deve mostrare le fusioni
diff loop_fusion_invalid.PRE.ll loop_fusion_invalid.OPT.ll   # deve essere vuoto (a parte il ModuleID)
```

Nel file *valid* dopo la fusione i corpi dei due loop finiscono sotto un unico
backedge (un solo header/latch), gli usi dell'IV del secondo loop sono rimappati
sull'IV del primo, e la vecchia struttura di controllo del secondo loop
scompare (rimossa da `EliminateUnreachableBlocks`).

Ispezione grafica del CFG (un `.dot` per funzione):

```bash
opt -passes=dot-cfg -cfg-dot-filename-prefix=valid loop_fusion_valid.OPT.ll
for f in valid*.dot; do dot -Tpng "$f" -o "${f%.dot}.png"; done   # richiede graphviz
```

Controllo che l'IR generato sia valido:

```bash
opt -passes=verify -disable-output loop_fusion_valid.OPT.ll   && echo "valid OK"
opt -passes=verify -disable-output loop_fusion_invalid.OPT.ll && echo "invalid OK"
```