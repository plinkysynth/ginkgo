#pragma once

char *stbstring_from_span(const char *start, const char * end = NULL, int alloc_extra= 0);
char *load_file(const char *path); // returns an stb_ds

#define temp_cstring_from_span(s, e) ({ size_t n=(e)-(s); char *t=(char*)alloca(n+1); memcpy(t,s,n); t[n]=0; t; }) // stack!

static inline int spancmp(const char *as, const char *ae, const char *bs, const char *be) {
    if (!ae) ae=as+strlen(as);
    if (!be) be=bs+strlen(bs);
    int alen = ae-as;
    int blen = be-bs;
    int cmp = memcmp(as, bs, (alen<blen)?alen:blen);
    if (cmp != 0)
        return cmp;
    return alen-blen;
}

static inline void spin_lock(atomic_flag *s) {
    while (atomic_flag_test_and_set_explicit(s, memory_order_acquire)) {
        // optional: backoff/yield here
    }
}

static inline void spin_unlock(atomic_flag *s) {
    atomic_flag_clear_explicit(s, memory_order_release);
}
