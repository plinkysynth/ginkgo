#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <atomic>
#include <utility>

// #define DEBUG_FETCH 1

int get_num_pending_fetch_jobs(void);

void sanitize_url_for_local_filesystem(const char *url, char *out, size_t cap) {
    size_t i = 0;
    for (; url[i] && i + 1 < cap; ++i) {
        out[i] =
            ((url[i] == '/' && url[i+1]=='/') || url[i] == ':' || url[i] == '<' || url[i] == '>' || url[i] == '?' || url[i] == '#' || url[i] == '&' || url[i] == '*' || url[i]=='|')
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

typedef void (*http_fetch_callback_t)(const char *url, const char *fname, void *userdata);

// --- curl global init ---


void ensure_curl_global() {
    static _Atomic(bool) g_curl_initialized = false;
    if (g_curl_initialized) return;
    g_curl_initialized = true;
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

// --- core fetch (sync, no callbacks, no statics) ---

static int fetch_to_cache_core(
    CURL *curl,              // may be NULL, then created and destroyed here
    const char *url,
    int prefer_offline,
    char *out_path,
    size_t out_cap
) {
    if (strncmp(url, "file://", 7) == 0) {
        snprintf(out_path, out_cap, "%s", url + 7);
        return 1;
    }
    if (!strstr(url,"://")) {
        snprintf(out_path, out_cap, "%s", url);
        return 1;
    }

    char name[1024];
    const char *cache_root = "webcache";
    sanitize_url_for_local_filesystem(url, name, sizeof name);

    snprintf(out_path, out_cap, "%s/%s", cache_root, name);

    if (prefer_offline) {
        struct stat st;
        if (stat(out_path, &st) == 0) {
#ifdef DEBUG_FETCH
            printf("web cache hit (prefer offline): %s\n", url);
#endif
            return 1;
        }
    }

    char *p = out_path;
    while (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = 0;
#ifdef __WINDOWS__
            mkdir(out_path);
#else
            mkdir(out_path, 0755);
#endif
            *p = '/';
        }
    }

    char etag_path[1200], tmp_path[1200];
    snprintf(etag_path, sizeof etag_path, "%s/%s.etag", cache_root, name);
    snprintf(tmp_path, sizeof tmp_path, "%s/%s.tmp", cache_root, name);

    char prev_etag[256] = {0};
    read_small(etag_path, prev_etag, sizeof prev_etag);

    ensure_curl_global();

    int created_curl = 0;
    if (!curl) {
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "curl_easy_init failed\n");
            return 0;
        }
        created_curl = 1;
    }

    struct curl_slist *hdrs = NULL;
    if (prev_etag[0]) {
        char h[300];
        snprintf(h, sizeof h, "If-None-Match: %s", prev_etag);
        hdrs = curl_slist_append(hdrs, h);
    }

    sink_t sink = {.fp = NULL, .fname = tmp_path};
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

    if (sink.fp) {
        fprintf(stderr, "[%d to go] fetched from %s\n", get_num_pending_fetch_jobs(), url);
        fclose(sink.fp);
    }

    if (created_curl)
        curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        remove(tmp_path);
        fprintf(stderr,"curl error: %d fetching %s\n", rc, url);
        return 0;
    }
    if (code == 304) {
        remove(tmp_path);
#ifdef DEBUG_FETCH
        printf("web cache hit: %s\n", url);
#endif
        return 1; // cache hit, unchanged
    } else if (code == 200) {
        if (hdr.etag[0])
            write_small(etag_path, hdr.etag);
        if (rename(tmp_path, out_path) != 0) {
            remove(tmp_path);
            fprintf(stderr,"failed to rename %s to %s\n", tmp_path, out_path);
            return 0;
        }
        return 1;
    } else {
        remove(tmp_path);
        fprintf(stderr,"unexpected HTTP status: %d fetching %s\n", (int)code, url);
        return 0;
    }
}

// --- sync wrapper (old behaviour, not thread-safe) ---

static const char *fetch_to_cache_sync(const char *url, int prefer_offline) {
    static char out_path[1024];
    if (strncmp(url, "file://", 7) == 0)
        return url + 7; // preserve old behaviour
    if (!strstr(url,"://"))
        return url;
    if (!fetch_to_cache_core(NULL, url, prefer_offline, out_path, sizeof out_path))
        return NULL;
    return out_path;
}

// --- async worker pool ---

struct FetchJob {
    std::string url;
    int prefer_offline;
    http_fetch_callback_t cb;
    void *userdata;
};

static std::mutex g_q_mtx;
static std::condition_variable g_q_cv;
static std::queue<FetchJob> g_q;
static std::atomic<bool> g_stop{false};
static std::once_flag g_workers_once;

int get_num_pending_fetch_jobs(void) {
    return (int)g_q.size();
}

static void fetch_worker_thread() {
    ensure_curl_global();
    CURL *curl = curl_easy_init();
    char out_path[1024];

    for (;;) {
        FetchJob job;
        {
            std::unique_lock<std::mutex> lk(g_q_mtx);
            g_q_cv.wait(lk, []{ return g_stop || !g_q.empty(); });
            if (g_stop && g_q.empty())
                break;
            job = std::move(g_q.front());
            g_q.pop();
        }

        const char *fname = NULL;

        if (strncmp(job.url.c_str(), "file://", 7) == 0) {
            fname = job.url.c_str() + 7;
        } else {
            if (fetch_to_cache_core(curl, job.url.c_str(),
                                    job.prefer_offline,
                                    out_path, sizeof out_path)) {
                fname = out_path;
            }
        }

        job.cb(job.url.c_str(), fname, job.userdata);
    }

    if (curl) curl_easy_cleanup(curl);
}

static void ensure_workers_started() {
    std::call_once(g_workers_once, []{
        const int N = 8; // max parallel downloads
        for (int i = 0; i < N; ++i)
            std::thread(fetch_worker_thread).detach();
    });
}

// --- public API ---

const char* fetch_to_cache(const char *url, int prefer_offline,
                           http_fetch_callback_t callback = 0,
                           void *userdata = 0)
{
    if (!callback) {
        // synchronous, original-style
        return fetch_to_cache_sync(url, prefer_offline);
    }

    // async: if file://, preserve old "immediate" behaviour
    if (strncmp(url, "file://", 7) == 0) {
        callback(url, url + 7, userdata);
        return NULL;
    }

    ensure_workers_started();

    {
        std::lock_guard<std::mutex> lk(g_q_mtx);
        g_q.push(FetchJob{
            std::string(url),
            prefer_offline,
            callback,
            userdata
        });
    }
    g_q_cv.notify_one();

    return NULL;
}
