#include <stdio.h>

// Test per StrengthReduction (tutto unsigned: mul senza flag, divisioni come udiv).
// Ogni riga e' scelta per attivare un ramo diverso del passo secondo il modello di costo.
//
//   MOLTIPLICAZIONI (mul costa 3):
//     a * 8   potenza di 2        -> a << 3
//     a * 15  del tipo 2^k - 1    -> (a << 4) - a
//     a * 9   caso generale       -> a + (a << 3)   (bit a 1 in pos. 0 e 3, costo 2)
//
//   DIVISIONI (udiv costa 10):
//     b / 8   potenza di 2        -> b >> 3
//     b / 10  numeri magici       -> mul alto + shift
//     b / 7   numeri magici       -> ramo "IsAdd" (correzione con sub/shift/add)
//     b / 6   numeri magici       -> ramo "PreShift" (divisore pari non potenza di 2)
unsigned foo(unsigned a, unsigned b) {
    unsigned m_pow2 = a * 8;    // a << 3
    unsigned m_sub  = a * 15;   // (a << 4) - a
    unsigned m_gen  = a * 9;    // a + (a << 3)

    unsigned d_pow2 = b / 8;    // b >> 3
    unsigned d_mag  = b / 10;   // magic
    unsigned d_add  = b / 7;    // magic, ramo IsAdd
    unsigned d_pre  = b / 6;    // magic, ramo PreShift

    printf("%u %u %u\n", m_pow2, m_sub, m_gen);
    printf("%u %u %u %u\n", d_pow2, d_mag, d_add, d_pre);
    return 0;
}
