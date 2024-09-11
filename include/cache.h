#include <curl/curl.h>
#include <utarray.h>
#include <uthash.h>

#ifndef CACHE_H

typedef enum {
	CACHE_NONE = 0,
	CACHE_REMOTE = 1,
	CACHE_LOCAL = 2,
} cache_state_t;

/* cache memoization */
struct cache_htab {
	char *hash;
	cache_state_t cache_state;
	UT_hash_handle hh;
};

struct cache {
	CURL *curl;
	UT_array *substituters;
	struct cache_htab *cache_htab;
};

void cache_free(struct cache *cache);
int cache_init(struct cache *cache);
int cache_state_read(struct cache *cache, const char *output_path);

#define CACHE_H
#endif
