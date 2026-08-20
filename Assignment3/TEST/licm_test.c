// Test per il passo Loop-Invariant Code Motion (`loop-invariant-motion`).
//
// A differenza dell'Assignment 1 qui la signedness NON conta: il passo lavora su
// qualsiasi BinaryOperator. Usiamo `unsigned int` solo per coerenza con gli altri
// test.
//
// NOTA FONDAMENTALE sulla forma del CFG (vale la condizione conservativa delle
// slide: un'istruzione si sposta solo se il suo blocco DOMINA tutte le uscite):
//   - In un loop "top-tested" (for/while classico) il corpo NON domina l'uscita,
//     perche' il loop puo' fare 0 iterazioni: le invarianti nel corpo NON si
//     spostano.
//   - In un loop "bottom-tested" (do-while) il corpo domina l'uscita: li' le
//     invarianti si spostano nel preheader.
// Per questo i casi 1-3 usano do-while. Il caso 4 e' volutamente top-tested per
// mostrare il comportamento conservativo.

// 1) Invariante semplice: `t = a * b` non dipende dal loop.
//    Attesa: `mul %a, %b` sale nel preheader.
unsigned int inv_semplice(unsigned int a, unsigned int b, unsigned int n) {
    unsigned int somma = 0;
    unsigned int i = 0;
    do {
        unsigned int t = a * b;   // loop-invariant -> hoist
        somma += t;
        i++;
    } while (i < n);
    return somma;
}

// 2) Cascata di invarianti: `u` dipende da `t`, entrambe invarianti.
//    Attesa: salgono ENTRAMBE, con `t` (add %a,%b) PRIMA di `u` (add t,%c)
//    nel preheader.
unsigned int inv_cascata(unsigned int a, unsigned int b, unsigned int c, unsigned int n) {
    unsigned int somma = 0;
    unsigned int i = 0;
    do {
        unsigned int t = a + b;   // invariante
        unsigned int u = t + c;   // invariante (dipende da t) -> cascade
        somma += u;
        i++;
    } while (i < n);
    return somma;
}

// 3) Discriminazione invariante / variante nello stesso loop.
//    Attesa: `p = a * b` sale; `q = i * a` resta (dipende dall'indice i).
unsigned int inv_e_variante(unsigned int a, unsigned int b, unsigned int n) {
    unsigned int somma = 0;
    unsigned int i = 0;
    do {
        unsigned int p = a * b;   // invariante -> hoist
        unsigned int q = i * a;   // dipende da i -> resta
        somma += p + q;
        i++;
    } while (i < n);
    return somma;
}

// 4) Loop top-tested: `t = a * b` E' invariante, ma il corpo non domina
//    l'uscita (0 iterazioni possibili).
//    Attesa: il passo la marca "potenzialmente loop-invariant" ma NON la sposta.
//    Comportamento corretto, non un bug. (Aggiungendo `loop-rotate` alla
//    pipeline questa salirebbe.)
unsigned int inv_non_dominante(unsigned int a, unsigned int b, unsigned int n) {
    unsigned int somma = 0;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int t = a * b;   // invariante ma resta nel corpo
        somma += t;
    }
    return somma;
}
