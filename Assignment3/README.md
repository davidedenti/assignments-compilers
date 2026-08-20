# Test – Assignment 3 (Loop-Invariant Code Motion)

Test in C per il passo `loop-invariant-motion`. Il file `.c` è il sorgente da
versionare; gli `.ll` sono **generati** al momento (per questo sono ignorati dal
`.gitignore`).

A differenza dell'Assignment 1 la signedness non conta: il passo lavora su
qualsiasi `BinaryOperator`. Qui `unsigned int` è usato solo per coerenza.

## File

- `licm_test.c` — quattro funzioni:
  - `inv_semplice` — invariante singola (`a*b`) → sale
  - `inv_cascata` — due invarianti in cascata (`t=a+b`, `u=t+c`) → salgono entrambe
  - `inv_e_variante` — invariante (`a*b`) sale, dipendente dall'indice (`i*a`) resta
  - `inv_non_dominante` — invariante in loop top-tested → **non** sale (conservativo)

## Come si genera l'IR ottimizzato

Ogni test passa per tre stadi:

1. **`clang`** → IR grezzo (`.O0.ll`)
2. **`opt -passes='mem2reg,loop-simplify'`** → forma SSA + loop in forma canonica
   (`.PRE.ll`)
3. **`opt -load-pass-plugin=...`** → applica il passo (`.OPT.ll`)

I flag di clang (identici all'Assignment 1):

- `-O0 -Xclang -disable-O0-optnone` — nessuna ottimizzazione del compilatore, ma
  senza l'attributo `optnone` che altrimenti bloccherebbe il nostro passo.
- `-emit-llvm -S` — emette IR testuale.
- `-fno-discard-value-names` — mantiene i nomi leggibili dei registri (`%t`, `%u`, …).

> **Perché `loop-simplify` (e non solo `mem2reg`)**
> Il passo chiama `L.getLoopPreheader()` e, se il preheader non c'è, esce senza
> fare nulla. `loop-simplify` garantisce la forma canonica del loop: un
> **preheader** dedicato, un solo latch e **exit block** dedicati (predecessori
> tutti interni al loop). Senza, sui loop non banali il passo non avrebbe dove
> spostare le istruzioni. È lo stesso preambolo che LLVM usa prima del suo LICM.

> **Nota sui loop top-tested (`for`/`while`)**
> Con la sola `mem2reg,loop-simplify` un `for`/`while` resta *top-tested*: il
> corpo non domina l'uscita (0 iterazioni possibili), quindi le invarianti nel
> corpo **non** vengono spostate — è la condizione conservativa delle slide. Per
> spostarle anche lì aggiungi `loop-rotate` alla pipeline
> (`mem2reg,loop-simplify,loop-rotate`): il loop diventa *bottom-tested* e il
> corpo domina l'uscita.

> I plugin (`.so`) prodotti da `add_llvm_pass_plugin` **non** hanno il prefisso
> `lib`. I comandi qui sotto assumono `BUILD/` accanto a `TEST/`.

### Comandi

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c licm_test.c -o licm_test.O0.ll \
 && opt -passes='mem2reg,loop-simplify' -S licm_test.O0.ll -o licm_test.PRE.ll \
 && opt -S -load-pass-plugin=../BUILD/LoopInvariantMotion.so -passes=loop-invariant-motion licm_test.PRE.ll -o licm_test.OPT.ll
```

Durante l'esecuzione il passo stampa su `stdout` due tipi di riga:

- `Istruzione potenzialmente loop-invariant: ...` — per ogni invariante trovata
- `Sposto nel preheader: ...` — solo per quelle effettivamente spostate

## Verifica dei risultati

Confronto prima/dopo (mostra solo lo spostamento operato dal passo, perché
`.PRE.ll` è già in SSA + forma canonica):

```bash
diff licm_test.PRE.ll licm_test.OPT.ll
```

Cosa ci si aspetta:

- **`inv_semplice`** — il `mul` (`%a * %b`) sparisce dal corpo e compare nel blocco
  preheader.
- **`inv_cascata`** — sia l'`add` `%a + %b` sia l'`add` `... + %c` compaiono nel
  preheader, con la prima **prima** della seconda (l'ordine rispetta la
  dipendenza).
- **`inv_e_variante`** — sale solo il `mul` `%a * %b`; il `mul` che coinvolge
  l'indice `i` resta nel corpo.
- **`inv_non_dominante`** — nessuno spostamento: nell'output del passo la riga
  "potenzialmente loop-invariant" compare, ma **non** la riga "Sposto nel
  preheader".

Controllo che l'IR generato sia valido:

```bash
opt -passes=verify -disable-output licm_test.OPT.ll && echo OK
```
