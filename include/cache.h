#include <uthash.h>
#include <utarray.h>
#include <curl/curl.h>

typedef enum {
	CACHE_LOCAL = 0,
	CACHE_REMOTE = 1,
	CACHE_NONE = 2,
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
int cache_state_remote_read(struct cache *cache, const char *drv_path);
