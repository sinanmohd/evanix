#include <errno.h>
#include <unistd.h>

#include "cache.h"
#include "util.h"

static void cache_htab_free(struct cache_htab *cache_htab);
static int cache_htab_new(struct cache_htab **cache_htab, char *hash);
static int hash_from_output_path(const char *drv_path, char **hash);
static size_t _curl_ignore_data(char __attribute__((unused)) * data,
				size_t size, size_t nmemb,
				__attribute__((unused)) void *userdata);
static int cache_state_remote_read_unwrapped(struct cache *cache,
					     struct cache_htab *cache_htab,
					     const char *substituter);
static int cache_state_local_read(const char *output_path);
static int cache_state_remote_read(struct cache *cache,
				   const char *output_path);

static void cache_htab_free(struct cache_htab *cache_htab)
{
	free(cache_htab->hash);
	free(cache_htab);
}

static int cache_htab_new(struct cache_htab **cache_htab, char *hash)
{
	int ret = 0;
	struct cache_htab *ch;

	ch = malloc(sizeof(*ch));
	if (ch == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	ch->hash = hash;
	ch->cache_state = CACHE_NONE;

	*cache_htab = ch;
	return ret;
}

void cache_free(struct cache *cache)
{
	struct cache_htab *i, *tmp;

	curl_easy_cleanup(cache->curl);

	HASH_ITER (hh, cache->cache_htab, i, tmp) {
		HASH_DEL(cache->cache_htab, i);
		cache_htab_free(i);
	}
	utarray_free(cache->substituters);
}

int cache_init(struct cache *cache)
{
	/* TODO: use Nix C API */
	char *cache_nixos_org = "https://cache.nixos.org/";
	utarray_new(cache->substituters, &ut_str_icd);
	utarray_push_back(cache->substituters, &cache_nixos_org);

	cache->curl = curl_easy_init();
	if (cache->curl == NULL) {
		printf("%s", "Failed to init curl");
		return -EPERM;
	}
	cache->cache_htab = NULL;

	return 0;
}

static int hash_from_output_path(const char *drv_path, char **hash)
{
	char *start, *end;
	int ret = 0;
	char *h;

	start = strrchr(drv_path, '/');
	if (start == NULL) {
		print_err("%s", "Failed to parse hash");
		return -EINVAL;
	}
	start += 1;

	end = strchr(start, '-');
	if (end == NULL) {
		print_err("%s", "Failed to parse hash");
		return -EINVAL;
	}

	h = strndup(start, end - start);
	if (h == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	*hash = h;
	return ret;
}

static size_t _curl_ignore_data(char __attribute__((unused)) * data,
				size_t size, size_t nmemb,
				__attribute__((unused)) void *userdata)
{
	return size * nmemb;
}

static int cache_state_remote_read_unwrapped(struct cache *cache,
					     struct cache_htab *cache_htab,
					     const char *substituter)
{
	int ret;
	char url[2048];
	long http_code;

	ret = snprintf(url, sizeof(url), "%s%s.narinfo", substituter,
		       cache_htab->hash);
	if (ret >= (int)sizeof(url)) {
		print_err("substituter URL too big: %s", substituter);
		return -ENOMEM;
	}
	ret = curl_easy_setopt(cache->curl, CURLOPT_URL, url);
	if (ret != CURLE_OK) {
		print_err("cURL: %s", curl_easy_strerror(ret));
		return -EPERM;
	}

	ret = curl_easy_setopt(cache->curl, CURLOPT_FOLLOWLOCATION, 1L);
	if (ret != CURLE_OK) {
		print_err("cURL: %s", curl_easy_strerror(ret));
		return -EPERM;
	}
	/* we just use the HTTP status code */
	ret = curl_easy_setopt(cache->curl, CURLOPT_WRITEFUNCTION,
			       _curl_ignore_data);
	if (ret != CURLE_OK) {
		print_err("cURL: %s", curl_easy_strerror(ret));
		return -EPERM;
	}

	ret = curl_easy_perform(cache->curl);
	if (ret != CURLE_OK) {
		print_err("cURL: %s", curl_easy_strerror(ret));
		return -EPERM;
	}

	ret = curl_easy_getinfo(cache->curl, CURLINFO_HTTP_CODE, &http_code);
	if (ret != CURLE_OK) {
		print_err("cURL: %s", curl_easy_strerror(ret));
		return -EPERM;
	}

	if (http_code == 200)
		cache_htab->cache_state = CACHE_REMOTE;

	return cache_htab->cache_state;
}

static int cache_state_local_read(const char *output_path)
{
	int ret = access(output_path, R_OK);
	if (ret == 0) {
		return CACHE_LOCAL;
	} else if (errno == ENOENT) {
		return CACHE_NONE;
	} else {
		print_err("%s", strerror(errno));
		return -errno;
	}
}

static int cache_state_remote_read(struct cache *cache, const char *output_path)
{
	int ret;
	char *hash, **substituter;
	struct cache_htab *ch;

	ret = hash_from_output_path(output_path, &hash);
	if (ret < 0)
		return ret;

	HASH_FIND_STR(cache->cache_htab, hash, ch);
	if (ch != NULL) {
		free(hash);
		return ch->cache_state;
	}

	ret = cache_htab_new(&ch, hash);
	if (ret < 0) {
		free(hash);
		return ret;
	}

	substituter = NULL;
	while (1) {
		substituter = utarray_next(cache->substituters, substituter);
		if (substituter == NULL)
			break;

		ret = cache_state_remote_read_unwrapped(cache, ch,
							*substituter);
		if (ret < 0)
			goto out_free_ch;
		else if (ret == CACHE_REMOTE)
			break;
	}

out_free_ch:
	if (ret < 0) {
		cache_htab_free(ch);
		return ret;
	} else {
		HASH_ADD_STR(cache->cache_htab, hash, ch);
		return ch->cache_state;
	}
}

int cache_state_read(struct cache *cache, const char *output_path)
{
	int ret;

	ret = cache_state_local_read(output_path);
	if (ret == CACHE_LOCAL)
		return ret;

	return cache_state_remote_read(cache, output_path);
}
