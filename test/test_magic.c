#include <stdio.h>
#include <stdint.h>

struct SignedMagic { int32_t magic; int shift; };

struct SignedMagic computeSignedMagic(int32_t d) {
    if (d == 0 || d == 1 || d == -1) return (struct SignedMagic){0, -1};
    if (d == INT32_MIN) return (struct SignedMagic){0, -1};
    const unsigned N = 32;
    uint32_t abs_d = (uint32_t)(d >= 0 ? d : -d);
    uint32_t ad = abs_d;
    uint32_t t = 0x80000000u + (uint32_t)(d >> 31);
    uint32_t anc = t - 1 - t % ad;
    unsigned p = N - 1;
    uint32_t q1 = 0x80000000u / anc;
    uint32_t r1 = 0x80000000u - q1 * anc;
    uint32_t q2 = 0x80000000u / ad;
    uint32_t r2 = 0x80000000u - q2 * ad;
    uint32_t delta;
    do {
        p++;
        q1 <<= 1; r1 <<= 1;
        if (r1 >= anc) { q1++; r1 -= anc; }
        q2 <<= 1; r2 <<= 1;
        if (r2 >= ad) { q2++; r2 -= ad; }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    struct SignedMagic m;
    m.magic = (int32_t)(q2 + 1);
    if (d < 0) m.magic = -m.magic;
    m.shift = (int)(p - N);
    return m;
}

int32_t test_sdiv(int32_t n, int32_t d) {
    struct SignedMagic m = computeSignedMagic(d);
    int64_t hi = (int64_t)n * m.magic;
    int32_t q = (int32_t)(hi >> 32);
    if (m.magic < 0) q += n;
    if (m.shift > 0) q >>= m.shift;
    return q;
}

int main() {
    int32_t divisors[] = {3, 5, 7, 10, 100, 131072, -5, -10};
    for (int j = 0; j < 8; j++) {
        int32_t d = divisors[j];
        struct SignedMagic m = computeSignedMagic(d);
        printf("d=%d magic=%d (0x%x) shift=%d\n", d, m.magic, (unsigned)m.magic, m.shift);
        int32_t test_vals[] = {0, 1, -1, d-1, d, d+1, -d+1, -d, -d-1, 1000000, -1000000};
        int fail = 0;
        for (int i = 0; i < 11; i++) {
            int32_t n = test_vals[i];
            int32_t expected = n / d;
            int32_t got = test_sdiv(n, d);
            if (expected != got) {
                printf("  FAIL: n=%d sdiv=%d magic=%d\n", n, expected, got);
                fail = 1;
            }
        }
        if (!fail) printf("  ALL OK\n");
    }
    return 0;
}