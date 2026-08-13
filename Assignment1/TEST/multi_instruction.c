#include <stdio.h>

// Test per MultiInstructionOptimization.
// Il tuo passo riconosce lo schema con COSTANTE:  a = b + C ; c = a - C  =>  c = b
// (l'offset deve essere una costante intera, non una variabile).
// Copre entrambe le forme della add: "b + C" e "C + b".
unsigned foo(unsigned b, unsigned d) {
    unsigned x = b + 1;   // b + C
    unsigned y = x - 1;   // (b + 1) - 1  -> b

    unsigned p = 7 + d;   // C + b  (add commutativa)
    unsigned q = p - 7;   // (7 + d) - 7  -> d

    printf("%u %u\n", y, q);
    return 0;
}
