#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include "3rdparty/stb_ds.h"
#include "utils.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
int mkdir_p(const char *path, bool include_last_path_component) {
    char path_copy[strlen(path) + 1];
    strcpy(path_copy, path);
    char *p = path_copy;
    while (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = 0;
#ifdef __WINDOWS__
            int i =mkdir(path_copy);
#else
            int i = mkdir(path_copy, 0755);
#endif
            if (i != 0 && errno!=EEXIST) {
                fprintf(stderr, "mkdir %s failed: %s\n", path_copy, strerror(errno));
                return i;
            }
            *p = '/';
        }
    }
    if (!include_last_path_component) 
        return 0;
#ifdef __WINDOWS__
    return mkdir(path_copy);
#else
    return mkdir(path_copy, 0755);
#endif
}

char *stbstring_printf(const char *fmt, ...) {
    va_list args1;
    va_start(args1, fmt);
    // Get the required size for the formatted string (excluding null terminator)
    int needed = vsnprintf(NULL, 0, fmt, args1);
    va_end(args1);
    if (needed < 0) return NULL;
    char *buf = NULL;
    stbds_arrsetlen(buf, needed + 1);
    va_list args2;
    va_start(args2, fmt);
    vsnprintf(buf, needed + 1, fmt, args2);
    va_end(args2);
    return buf;
}

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
