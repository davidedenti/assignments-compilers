# Test – Assignment 4 (Loop Fusion)

Test in C per il passo `loop-fusion-opt`. I file `.c` sono i sorgenti da
versionare; gli `.ll` (e gli eventuali `.dot`) sono **generati** al momento e
vanno ignorati dal `.gitignore`.

Ogni file raccoglie piu' funzioni, una per scenario: `opt` esegue il passo su
tutte le funzioni del modulo in un colpo solo.

## File

- `loop_fusion_valid.c` — la fusione e' **legale**, il passo deve fondere:
  - `fuse_indipendenti` — array diversi, nessuna dipendenza
  - `fuse_dipendenza_distanza_zero` — il 2° loop legge `a[i]` scritto dal 1° nella stessa iterazione (RAW distanza 0)
  - `fuse_tre_loop` — tre loop adiacenti a stesso trip count (fusione a cascata)
- `loop_fusion_invalid.c` — la fusione e' **illegale**, il passo NON deve fondere (una violazione per funzione):
  - `non_adiacenti` — statement tra i due loop → viola l'adiacenza
  - `trip_count_diverso` — `N` vs `N/2` → viola lo stesso trip count
  - `non_cfg_equivalenti` — 2° loop dentro un `if` → viola la control-flow equivalence
  - `dipendenza_negativa` — il 2° loop legge `a[i+1]` (elemento futuro) → dipendenza a distanza negativa

Perche' array **locali** e non `int *`: due `alloca` distinti sono noti come
non-aliasing, quindi la `DependenceAnalysis` e' precisa. Con puntatori-parametro
l'analisi assume possibile aliasing, diventa conservativa e bloccherebbe anche
le fusioni legali. I bound sono **costanti** (`N`) cosi' `ScalarEvolution`
calcola un trip count costante.

## Come si genera l'IR

Due stadi (più leggero del LICM: qui basta `mem2reg`, non serve `loop-simplify`):

1. **`clang`** → IR grezzo (`.O0.ll`)
2. **`opt -passes=mem2reg`** → forma SSA, promuovendo gli scalari da memoria a registri

Flag di clang (gli stessi degli altri assignment):

- `-O0 -Xclang -disable-O0-optnone` — nessuna ottimizzazione del compilatore, ma
  senza l'attributo `optnone` che bloccherebbe il nostro passo.
- `-emit-llvm -S` — IR testuale.
- `-fno-discard-value-names` — nomi di registri e blocchi leggibili.

> Il plugin (`.so`) prodotto da `add_llvm_pass_plugin` **non** ha il prefisso
> `lib`. I comandi assumono `BUILD/` accanto a `TEST/`. Adatta il nome del
> plugin (`LoopFusion.so`) e del passo (`loop-fusion-opt`) a quelli del tuo
> build.

### loop_fusion_valid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_valid.c -o loop_fusion_valid.O0.ll \
 && opt -passes=mem2reg -S loop_fusion_valid.O0.ll -o loop_fusion_valid.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-opt loop_fusion_valid.O0.ll -o loop_fusion_valid.OPT.ll
```

### loop_fusion_invalid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_invalid.c -o loop_fusion_invalid.O0.ll \
 && opt -passes=mem2reg -S loop_fusion_invalid.O0.ll -o loop_fusion_invalid.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-opt loop_fusion_invalid.O0.ll -o loop_fusion_invalid.OPT.ll
```

## Verifica dei risultati

La Loop Fusion ristruttura il CFG, quindi un `diff` testuale e' rumoroso: meglio
guardare il grafo. Genera i CFG (un `.dot` per funzione, col nome della funzione):

```bash
opt -passes=dot-cfg -cfg-dot-filename-prefix=valid   loop_fusion_valid.OPT.ll
opt -passes=dot-cfg -cfg-dot-filename-prefix=invalid loop_fusion_invalid.OPT.ll
# opzionale: converti in PNG (richiede graphviz)
for f in *.dot; do dot -Tpng "$f" -o "${f%.dot}.png"; done
```

Cosa ci si aspetta:

- **`loop_fusion_valid.*`** — i corpi dei due (o tre) loop finiscono sotto un
  **unico** backedge: un solo header/latch invece di due, e gli usi della
  variabile d'induzione del secondo loop rimappati su quella del primo.
- **`loop_fusion_invalid.*`** — la struttura resta a **due loop separati**:
  nessuna fusione.

Se il tuo passo stampa messaggi diagnostici, compaiono su `stdout` durante il
terzo comando `opt`.

Controllo che l'IR generato sia valido:

```bash
opt -passes=verify -disable-output loop_fusion_valid.OPT.ll   && echo "valid OK"
opt -passes=verify -disable-output loop_fusion_invalid.OPT.ll && echo "invalid OK"
```

## Variante su loop ruotati (extra)

Se implementi anche la versione che lavora solo su *rotated loops*, gli stessi
`.c` vanno bene: aggiungi `loop-rotate` alla pipeline dopo `mem2reg` e usa il
plugin/nome-passo della variante, ad esempio:

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_valid.c -o loop_fusion_valid_rot.O0.ll \
 && opt -passes='mem2reg,loop-rotate' -S loop_fusion_valid_rot.O0.ll -o loop_fusion_valid_rot.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusionRotated.so -passes=loop-fusion-r-opt loop_fusion_valid_rot.O0.ll -o loop_fusion_valid_rot.OPT.ll
```