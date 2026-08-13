#include <stdio.h>

// Test per AlgebricIdentity.
// unsigned -> clang NON emette nsw/nuw, quindi il passo lavora su add/mul "pulite".
// Ottimizzazioni attese:
//   a + 0  -> a
//   0 + b  -> b
//   a * 1  -> a
//   1 * b  -> b
unsigned foo(unsigned a, unsigned b) {
    unsigned add_r = a + 0;   // x + 0 -> x
    unsigned add_l = 0 + b;   // 0 + x -> x
    unsigned mul_r = a * 1;   // x * 1 -> x
    unsigned mul_l = 1 * b;   // 1 * x -> x

    // usiamo i risultati cosi' non vengono eliminati come codice morto
    unsigned s = add_r + add_l;
    unsigned m = mul_r * mul_l;

    printf("%u %u\n", s, m);
    return 0;
}
