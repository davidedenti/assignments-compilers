# Test – Assignment 1

Test in C per i tre passi. I file `.c` sono il sorgente da versionare; gli `.ll`
sono **generati** al momento (per questo sono ignorati dal `.gitignore`).

I test usano `unsigned int` di proposito: così clang **non** emette i flag
`nsw`/`nuw` e le divisioni diventano `udiv`, cioè esattamente le forme su cui i
passi lavorano.

## File

- `algebraic_identity.c` — `x+0`, `0+x`, `x*1`, `1*x`
- `multi_instruction.c` — `(b + C) - C  ⇒  b` (in entrambe le forme della add)
- `strength_reduction.c` — `mul`/`udiv` per costante → shift/add/sub e numeri magici

## Come si genera l'IR ottimizzato

Ogni test passa per tre stadi:

1. **`clang`** → IR grezzo (`.O0.ll`)
2. **`opt -passes=mem2reg`** → forma SSA (promuove le variabili da memoria a registri)
3. **`opt -load-pass-plugin=...`** → applica il passo (`.OPT.ll`)

I flag usati:

- `-O0 -Xclang -disable-O0-optnone` — nessuna ottimizzazione del compilatore, ma
  senza l'attributo `optnone` che altrimenti bloccherebbe i nostri passi.
- `-emit-llvm -S` — emette IR testuale.
- `-fno-discard-value-names` — mantiene i nomi leggibili dei registri (`%add`, …).

Il passaggio `mem2reg` è necessario: senza, a `-O0` le variabili restano in
memoria (`alloca`/`load`/`store`) e le operazioni binarie non comparirebbero come
tali, quindi i passi non troverebbero nulla da ottimizzare.

> I plugin (`.so`) prodotti da `add_llvm_pass_plugin` **non** hanno il prefisso
> `lib`. I comandi qui sotto assumono `BUILD/` accanto a `TEST/`.

### Algebraic Identity

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c algebraic_identity.c -o algebraic_identity.O0.ll \
 && opt -passes=mem2reg -S algebraic_identity.O0.ll -o algebraic_identity.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/AlgebricIdentity.so -passes=algebric-identity algebraic_identity.O0.ll -o algebraic_identity.OPT.ll
```

### Multi-Instruction Optimization

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c multi_instruction.c -o multi_instruction.O0.ll \
 && opt -passes=mem2reg -S multi_instruction.O0.ll -o multi_instruction.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/MultiInstructionOptimization.so -passes=multi-instruction-optimization multi_instruction.O0.ll -o multi_instruction.OPT.ll
```

### Strength Reduction

```bash
clang -O0 -Xclang -disable-O0-optnone -emit-llvm -fno-discard-value-names -S -c strength_reduction.c -o strength_reduction.O0.ll \
 && opt -passes=mem2reg -S strength_reduction.O0.ll -o strength_reduction.O0.ll \
 && opt -S -load-pass-plugin=../BUILD/StrengthReduction.so -passes=strength-reduction strength_reduction.O0.ll -o strength_reduction.OPT.ll
```

## Verifica dei risultati

Confronto prima/dopo:

```bash
for n in algebraic_identity multi_instruction strength_reduction; do
  echo "==== $n ===="; diff $n.O0.ll $n.OPT.ll
done
```

Cosa ci si aspetta:

- **algebraic_identity** — spariscono `add i32 %a, 0` e `mul i32 %a, 1`; gli usi
  puntano direttamente a `%a`/`%b`.
- **multi_instruction** — spariscono le `sub`; `y` diventa `%b`, `q` diventa `%d`.
- **strength_reduction** — `mul ,8` → `shl 3`; `mul ,15` → `shl 4` + `sub`;
  `mul ,9` → `shl 3` + `add`; `udiv ,8` → `lshr 3`; le divisioni per 10/7/6 →
  moltiplicazione a doppia larghezza per un numero magico + shift.

Controllo opzionale che l'IR generato sia valido:

```bash
opt -passes=verify -disable-output strength_reduction.OPT.ll && echo OK
```
