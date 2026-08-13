// Casi in cui la Loop Fusion e' LEGALE.
// I loop sono adiacenti, hanno lo stesso trip count, sono control-flow
// equivalent e non hanno dipendenze a distanza negativa: il passo DEVE fonderli.
//
// Gli array sono LOCALI di proposito: due alloca distinti sono noti come
// non-aliasing, quindi la DependenceAnalysis e' precisa. Con puntatori passati
// come parametro l'analisi sarebbe conservativa (possibile alias) e
// bloccherebbe la fusione anche quando e' legale.
//
// I bound sono costanti (N): cosi' ScalarEvolution calcola un trip count
// costante e la condizione "stesso numero di iterazioni" e' verificabile.

#define N 100

// 1) Due loop indipendenti su array diversi.
//    Nessuna dipendenza tra i corpi -> fondibili.
void fuse_indipendenti(void) {
    int a[N];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    for (int i = 0; i < N; i++)
        b[i] = i * 2;
}

// 2) Dipendenza a distanza 0: il secondo loop legge a[i], scritto dal primo
//    alla STESSA iterazione. Dopo la fusione a[i] e' gia' stato calcolato
//    nello stesso giro -> fusione legale.
void fuse_dipendenza_distanza_zero(void) {
    int a[N];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i + 1;

    for (int i = 0; i < N; i++)
        b[i] = a[i] + 3;
}

// 3) Tre loop adiacenti con lo stesso trip count.
//    Un passo che itera la fusione dovrebbe collassarli in un unico loop
//    (almeno la prima coppia se il passo fonde solo due loop per volta).
void fuse_tre_loop(void) {
    int a[N];
    int b[N];
    int c[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    for (int i = 0; i < N; i++)
        b[i] = i;

    for (int i = 0; i < N; i++)
        c[i] = i;
}
