#include <stdio.h>

// Test per StrengthReduction.
// Ogni riga e' scelta per attivare un ramo diverso del passo secondo il modello di costo.
//
//   MOLTIPLICAZIONI:
//     a * 8   potenza di 2        -> a << 3
//     a * 15  del tipo 2^k - 1    -> (a << 4) - a
//     a * 9   caso generale       -> a + (a << 3)
//
//   DIVISIONI:
//     b / 8   potenza di 2        -> b >> 3
unsigned foo(unsigned a, unsigned b) {
    unsigned m_pow2 = a * 8;    // a << 3
    unsigned m_sub  = a * 15;   // (a << 4) - a
    unsigned m_gen  = a * 9;    // a + (a << 3)

    unsigned d_pow2 = b / 8;    // b >> 3

    printf("%u %u %u\n", m_pow2, m_sub, m_gen);
    printf("%u\n", d_pow2);
    return 0;
}