#pragma once

// this string hash function only accepts strings up to 16 chars, intended for reserved words etc
// it however can be evaluated at compile time, so it can be used in case statements.


#define UC(lit, i) (((size_t)(unsigned char)((lit)[i]))<<(i*8))
#define UC8(x) (UC(x,0) + UC(x,1) + UC(x,2) + UC(x,3) + UC(x,4) + UC(x,5) + UC(x,6) + UC(x,7))
#define HASH_(x) (UC8(x) + UC8(x+8) * 1327217884ull)
#define HASH(lit) (HASH_(lit "                ")) /* 16 spaces */


static inline size_t literal_hash_span(const char *s, const char *e) {
    char buf[17]="                ";
    memcpy(buf, s, e-s);
    return HASH_(buf);
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

