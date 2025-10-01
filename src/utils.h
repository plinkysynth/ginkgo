#pragma once

char *make_cstring_from_span(const char *start, const char * end, int alloc_extra);
char *load_file(const char *path); // returns an stb_ds

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
