# Test – Assignment 4 (Loop Fusion)

Test in C per il passo `loop-fusion-pass`. I file `.c` sono i sorgenti da
versionare; gli `.ll` sono **generati** al momento e vanno ignorati dal
`.gitignore`.

Ogni file raccoglie più funzioni, una per scenario: `opt` esegue il passo su
tutte le funzioni del modulo in un colpo solo, indipendentemente l'una
dall'altra (il passo lavora per `Function`).

> **Stato del passo: work in progress.**
> Le verifiche di legalità (`canFuse`) sono implementate; la trasformazione
> (`fuseLoops`) è ancora un TODO. Quindi al momento il passo **non modifica
> l'IR**: si limita a stampare, per ogni coppia di loop innermost consecutivi,
> se sono fondibili o meno. La verifica dei test si fa quindi leggendo lo
> **stdout**, non confrontando gli `.ll`.

## File

- `loop_fusion_valid.c` — la fusione è **legale**, il passo deve stampare *«I loop possono essere fusi»*:
  - `fuse_indipendenti` — array diversi, nessuna dipendenza
  - `fuse_dipendenza_distanza_zero` — il 2° loop legge `a[i]` scritto dal 1° nella stessa iterazione (RAW distanza 0)
  - `fuse_tre_loop` — tre loop adiacenti a stesso trip count (due coppie consecutive analizzate: L0–L1 e L1–L2)
- `loop_fusion_invalid.c` — la fusione è **illegale**, il passo deve stampare *«I loop non possono essere fusi»* (una violazione per funzione):
  - `non_adiacenti` — statement tra i due loop → viola l'adiacenza
  - `trip_count_diverso` — `N` vs `N/2` → viola lo stesso trip count
  - `non_cfg_equivalenti` — 2° loop dentro un `if` → viola la control-flow equivalence (nel codice viene già scartato al controllo di adiacenza, l'esito è comunque «non fondibili»)
  - `dipendenza_negativa` — il 2° loop legge `a[i+1]` (elemento futuro) → dipendenza a distanza negativa; stampa anche *«Dipendenza backward»*

Perché array **locali** e non `int *`: due `alloca` distinti sono noti come
non-aliasing, quindi la `DependenceAnalysis` è precisa. Con puntatori-parametro
l'analisi assume possibile aliasing, diventa conservativa e la
`hasNegativeDistanceDependence` bloccherebbe anche le fusioni legali. I bound
sono **costanti** (`N`) così `ScalarEvolution` calcola un trip count costante.

## Come si genera l'IR

Due stadi (basta `mem2reg`, non serve `loop-simplify`):

1. **`clang`** → IR grezzo (`.O0.ll`)
2. **`opt -passes=mem2reg`** → forma SSA, promuovendo gli scalari da memoria a registri

Flag di clang (gli stessi degli altri assignment):

- `-O0 -Xclang -disable-O0-optnone` — nessuna ottimizzazione del compilatore, ma
  senza l'attributo `optnone` che bloccherebbe il nostro passo.
- `-emit-llvm -S` — IR testuale.
- `-fno-discard-value-names` — nomi di registri e blocchi leggibili (utili
  nell'output, es. `for.body`).

> Il plugin (`.so`) prodotto da `add_llvm_pass_plugin` **non** ha il prefisso
> `lib`. I comandi assumono `BUILD/` accanto a `TEST/`.

### loop_fusion_valid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_valid.c -o loop_fusion_valid.O0.ll \
 && opt -passes=mem2reg -S loop_fusion_valid.O0.ll -o loop_fusion_valid.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-pass loop_fusion_valid.O0.ll -o loop_fusion_valid.OPT.ll
```

### loop_fusion_invalid.c

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c loop_fusion_invalid.c -o loop_fusion_invalid.O0.ll \
 && opt -passes=mem2reg -S loop_fusion_invalid.O0.ll -o loop_fusion_invalid.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopFusion.so -passes=loop-fusion-pass loop_fusion_invalid.O0.ll -o loop_fusion_invalid.OPT.ll
```

## Verifica dei risultati

Finché `fuseLoops` è un TODO, l'`.OPT.ll` è identico all'input: la verifica è
sullo **stdout** del terzo comando `opt`. Per ogni coppia analizzata il passo
stampa una riga `Analizzo possibile fusione tra: <hdr0> e <hdr1>` seguita
dall'esito.

Atteso su `loop_fusion_valid.c` — ogni coppia riporta:

```
I loop possono essere fusi
```

Atteso su `loop_fusion_invalid.c` — ogni coppia riporta:

```
I loop non possono essere fusi
```

(per `dipendenza_negativa` preceduto da `Dipendenza backward`).

Controllo di sanità che l'IR sia comunque valido:

```bash
opt -passes=verify -disable-output loop_fusion_valid.OPT.ll   && echo "valid OK"
opt -passes=verify -disable-output loop_fusion_invalid.OPT.ll && echo "invalid OK"
```

> Quando `fuseLoops` sarà implementato, la verifica passerà dal confronto dello
> stdout all'ispezione del CFG: genera i grafi con
> `opt -passes=dot-cfg -cfg-dot-filename-prefix=<pref> <file>.OPT.ll` e verifica
> che nei casi *valid* i corpi finiscano sotto un unico backedge.

## Variante su loop ruotati (extra)

Se implementi anche la versione che lavora solo su *rotated loops*, gli stessi
`.c` vanno bene: aggiungi `loop-rotate` alla pipeline dopo `mem2reg` e usa
plugin/nome-passo di quella variante (verifica il nome reale con
`grep -rn 'Name ==' ../SRC`).