#pragma once
typedef void (*http_fetch_callback_t)(const char *url, const char *fname, void *userdata);
const char *fetch_to_cache(const char *url, int prefer_offline, http_fetch_callback_t callback=0, void *userdata=0); // from http_fetch.c
void pump_done_fetch_jobs(void);
void ensure_curl_global();
int get_num_pending_fetch_jobs(void);