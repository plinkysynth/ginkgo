#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include "3rdparty/stb_ds.h"
#include "utils.h"

char *stbstring_from_span(const char *s, const char *e, int alloc_extra) {
    if (s && !e) e = s + strlen(s);
    int len = e - s;
    if (len < 0)
        return NULL;
    char *cstr = NULL;
    stbds_arrsetlen(cstr, len + 1 + alloc_extra);
    memcpy(cstr, s, len);
    memset(cstr + len, 0, 1 + alloc_extra);
    return cstr;
}

char *load_file(const char *path) { // returns an stb_ds
    if (!path) return NULL;
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    char *str=NULL;
    stbds_arrsetlen(str, len);
    fseek(f, 0, SEEK_SET);
    fread(str, 1, len, f);
    fclose(f);
    return str;
}
