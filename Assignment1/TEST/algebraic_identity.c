#include <stdio.h>

// Test per AlgebricIdentity.
// unsigned -> clang NON emette nsw/nuw, quindi il passo lavora su add/mul "pulite".
// Ottimizzazioni attese:
//   x + 0  -> x
//   0 + y  -> y
//   x * 1  -> x
//   1 * y  -> y
unsigned foo(unsigned x, unsigned y) {
    unsigned add_a = x + 0;
    unsigned add_b = 0 + y;
    unsigned mul_a = x * 1;
    unsigned mul_b = 1 * y;

    // usiamo i risultati cosi' non vengono eliminati come codice morto (dce)
    unsigned s = add_a + add_b;
    unsigned m = mul_a * mul_b;

    printf("%u %u\n", s, m);
    return 0;
}
