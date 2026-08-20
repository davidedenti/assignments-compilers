// Casi in cui la Loop Fusion NON e' legale.
// Ogni funzione viola UNA sola delle quattro condizioni: il passo NON deve
// fondere. Servono a verificare che i controlli di legalita' blocchino
// correttamente la trasformazione.

#define N 100

// VIOLA (1) ADIACENZA.
// Tra i due loop c'e' un'istruzione (una store): l'exit del primo loop non
// coincide piu' con il preheader del secondo -> non adiacenti.
void non_adiacenti(void) {
    int a[N];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    b[0] = 7;               // <-- statement tra i due loop

    for (int i = 0; i < N; i++)
        b[i] = i;
}

// VIOLA (2) STESSO TRIP COUNT.
// Il primo loop itera N volte, il secondo N/2: trip count diversi.
void trip_count_diverso(void) {
    int a[N];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    for (int i = 0; i < N / 2; i++)
        b[i] = i;
}

// VIOLA (3) CONTROL-FLOW EQUIVALENCE.
// Il secondo loop e' dentro un if: il primo loop non e' post-dominato dal
// secondo (si puo' raggiungere l'uscita senza eseguirlo) -> non equivalenti
// nel control flow.
void non_cfg_equivalenti(int cond) {
    int a[N];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    if (cond) {
        for (int i = 0; i < N; i++)
            b[i] = i;
    }
}

// VIOLA (4) DIPENDENZA A DISTANZA NEGATIVA.
// Stesso trip count, adiacenti, control-flow equivalent, MA il secondo loop
// legge a[i+1]: un elemento che il primo loop (una volta fusi) calcolerebbe
// solo all'iterazione SUCCESSIVA. Il valore non sarebbe disponibile.
// a e' dimensionato N+1 per evitare accessi fuori dai limiti.
void dipendenza_negativa(void) {
    int a[N + 1];
    int b[N];

    for (int i = 0; i < N; i++)
        a[i] = i;

    for (int i = 0; i < N; i++)
        b[i] = a[i + 1] + 1;
}
