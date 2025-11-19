#pragma once

// this string hash function only accepts strings up to 16 chars, intended for reserved words etc
// it however can be evaluated at compile time, so it can be used in case statements.

#define UC(lit, i, p) ((unsigned)p * (unsigned)(unsigned char)((lit)[i]))

#define HASH_(x)                                                                                                                   \
    UC(x, 0, 257) + UC(x, 1, 263) + UC(x, 2, 269) + UC(x, 3, 271) + UC(x, 4, 277) + UC(x, 5, 281) + UC(x, 6, 283) + UC(x, 7, 293) +            \
        UC(x, 8, 307) + UC(x, 9, 311) + UC(x, 10, 313) + UC(x, 11, 317) + UC(x, 12, 331) + UC(x, 13, 337) + UC(x, 14, 347) +              \
        UC(x, 15, 349)

#define HASH(lit) HASH_(lit "                ") /* 16 spaces */

static inline unsigned literal_hash_span(const char *s, const char *e) {
    static const unsigned primes[16] = {257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349};
    unsigned h = 0;
    for (int i = 0; i < 16; ++i, ++s) {
        unsigned c = (s < e) ? *s : ' ';
        h += c * primes[i];
    }
    return h;
}

// FNV-1 32-bit string hash (not compile-time, but fast and simple)
static inline unsigned fnv1_hash(const char *s, const char *e=NULL) {
    if (!e && s) e=s+strlen(s);
    unsigned h = 2166136261u;
    for (;s<e;++s) {
        h *= 16777619u;
        h ^= (unsigned char)*s;
    }
    return h;
}

#define CASE(x) case HASH(x)
#define CASE2(x, y)                                                                                                                \
    case HASH(x):                                                                                                                  \
    case HASH(y)
#define CASE4(x, y, z, w) CASE2(x, y) : CASE2(z, w)
#define CASE6(x, y, z, w, a, b) CASE2(x, y) : CASE2(z, w) : CASE2(a, b)
#define CASE8(a, b, c, d, e, f, g, h) CASE4(a, b, c, d) : CASE4(e, f, g, h)

