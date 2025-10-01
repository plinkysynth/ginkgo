#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
// #define DEBUG_FETCH 1

static void sanitize_url(const char *url, char *out, size_t cap) {
    size_t i = 0;
    for (; url[i] && i + 1 < cap; ++i) {
        out[i] =
            (url[i] == '/' || url[i] == ':' || url[i] == '.' || url[i] == '?' || url[i] == '#' || url[i] == '&' || url[i] == '*')
                ? '_'
                : url[i];
    }
    out[i] = 0;
}

typedef struct {
    FILE *fp;
    const char *fname;
} sink_t;

static size_t write_body(char *ptr, size_t sz, size_t nm, void *ud) { 
    sink_t *sink = (sink_t *)ud;
    if (sink->fname && !sink->fp) {
        sink->fp = fopen(sink->fname, "wb");
        if (!sink->fp)
            return 0;
    }
    return fwrite(ptr, sz, nm, sink->fp);
}

typedef struct {
    char etag[256];
} hdr_t;

static size_t capture_etag(char *ptr, size_t sz, size_t nm, void *ud){
    size_t n = sz*nm; if (n < 7) return n;
    // case-insensitive matches "ETag:"
    if (!strncasecmp(ptr, "ETag:", 5)){
        char *p = ptr + 5;
        while (p < ptr+n && (*p==' '||*p=='\t')) p++;
        size_t m = (size_t)(ptr + n - p);
        if (m >= sizeof(((hdr_t*)ud)->etag)) m = sizeof(((hdr_t*)ud)->etag)-1;
        memcpy(((hdr_t*)ud)->etag, p, m);
        // strip only trailing CR/LF/space
        while (m && (((hdr_t*)ud)->etag[m-1]=='\r' || ((hdr_t*)ud)->etag[m-1]=='\n' || ((hdr_t*)ud)->etag[m-1]==' '))
            m--;
        ((hdr_t*)ud)->etag[m] = 0;
    }
    return n;
}

static int read_small(const char *p, char *buf, size_t cap) {
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
    fclose(f);
    return (int)n;
}
static void write_small(const char *p, const char *s) {
    FILE *f = fopen(p, "wb");
    if (!f)
        return;
    fwrite(s, 1, strlen(s), f);
    fclose(f);
}

const char*fetch_to_cache(const char *url, int prefer_offline) { // returns the path to the cached file
    static char out_path[1024];
    static CURL *curl;
    size_t out_cap = sizeof out_path;
    char name[1024];
    const char *cache_root = "webcache";
    sanitize_url(url, name, sizeof name);
    
    snprintf(out_path, out_cap, "%s/%s", cache_root, name);
    if (prefer_offline) {
        struct stat st;
        if (stat(out_path, &st) == 0) {
#ifdef DEBUG_FETCH
            printf("web cache hit (prefer offline): %s\n", url);
#endif
            return out_path;
        }
    }

    mkdir(cache_root, 0755); // best-effort

    char etag_path[1200], tmp_path[1200];
    snprintf(etag_path, sizeof etag_path, "%s/%s.etag", cache_root, name);
    snprintf(tmp_path, sizeof tmp_path, "%s/%s.tmp", cache_root, name);


    char prev_etag[256] = {0};
    read_small(etag_path, prev_etag, sizeof prev_etag);

    
    if (!curl) curl = curl_easy_init();
    if (!curl)
        return NULL;
    struct curl_slist *hdrs = NULL;
    if (prev_etag[0]) {
        char h[300];
        snprintf(h, sizeof h, "If-None-Match: %s", prev_etag);
        hdrs = curl_slist_append(hdrs, h);
    }

    sink_t sink = {.fname = tmp_path };
    hdr_t hdr = {{0}};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, capture_etag);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hdr);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-fetch/1.1");
#ifdef DEBUG_FETCH
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    // persistent curl object so no curl_easy_cleanup(curl);
    if (sink.fp) {
        fprintf(stderr,"fetched from %s\n", url);
        fclose(sink.fp);
    }
    if (rc != CURLE_OK) {
        remove(tmp_path);
        fprintf(stderr,"curl error: %d fetching %s\n", rc, url);
        return NULL;
    }
    if (code == 304) {
        remove(tmp_path);
#ifdef DEBUG_FETCH
        printf("web cache hit: %s\n", url);
#endif
        return out_path; // cache hit, unchanged
    } else if (code == 200) {
        if (hdr.etag[0])
            write_small(etag_path, hdr.etag);
        // atomically update cache
        if (rename(tmp_path, out_path) != 0) {
            remove(tmp_path);
            fprintf(stderr,"failed to rename %s to %s\n", tmp_path, out_path);
            return NULL;
        }
        return out_path;
    } else {
        remove(tmp_path);
        fprintf(stderr,"unexpected HTTP status: %d fetching %s\n", (int)code, url);
        return NULL; // unexpected HTTP status
    }
}

