/* rng.h - deterministic xoshiro256** PRNG (header-only). Same seed -> same universe. */
#ifndef ORBIT_RNG_H
#define ORBIT_RNG_H

#include <math.h>
#include <stdint.h>

typedef struct {
    uint64_t s[4];
} rng;

static inline uint64_t rng__splitmix(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline void rng_seed(rng *r, uint64_t seed) {
    for (int i = 0; i < 4; i++) r->s[i] = rng__splitmix(&seed);
}

static inline uint64_t rng__rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static inline uint64_t rng_u64(rng *r) {
    uint64_t *s = r->s;
    uint64_t result = rng__rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rng__rotl(s[3], 45);
    return result;
}

/* Uniform in [0, 1). */
static inline double rng_double(rng *r) { return (double)(rng_u64(r) >> 11) * (1.0 / 9007199254740992.0); }
/* Uniform in [lo, hi). */
static inline double rng_range(rng *r, double lo, double hi) { return lo + (hi - lo) * rng_double(r); }
/* Standard normal (Box-Muller). */
static inline double rng_normal(rng *r) {
    double u1 = rng_double(r), u2 = rng_double(r);
    if (u1 < 1e-300) u1 = 1e-300;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

#endif
