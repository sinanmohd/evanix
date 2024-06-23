/* since hsearch_r does not support deletions dirctly (gotta go fast), this is
 * an abstraction on top of it with support for deletions, for this to work
 * properly we have to strdup the key, this is not a big issue for 100k drv_path
 * strings, it's either this or pulling in an external library
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "htab.h"
#include "util.h"

static int htab_keys_insert(struct htab *htab, char *key);

static int htab_keys_insert(struct htab *htab, char *key)
{
	size_t newsize;
	void *ret;

	if (htab->key_filled < htab->keys_size) {
		htab->keys[htab->key_filled++] = key;
		return 0;
	}

	newsize = htab->keys_size == 0 ? 1 : htab->keys_size * 2;
	ret = realloc(htab->keys, newsize * sizeof(*htab->keys));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	htab->keys = ret;
	htab->keys_size = newsize;
	htab->keys[htab->key_filled++] = key;

	return 0;
}

int htab_search(struct htab *htab, char *key, ENTRY **ep)
{
	ENTRY e;
	int ret;

	e.key = key;
	e.data = NULL;
	ret = hsearch_r(e, FIND, ep, htab->table);
	if (ret == 0) {
		if (errno != ESRCH) {
			print_err("%s", strerror(errno));
			return -errno;
		}
		return ESRCH;
	}

	if ((*ep)->data == NULL) {
		return ESRCH;
	} else {
		return 0;
	}
}

int htab_enter(struct htab *htab, const char *key, void *data)
{
	ENTRY e, *ep;
	int ret;

	e.key = strdup(key);
	if (e.key == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	e.data = data;
	ret = hsearch_r(e, ENTER, &ep, htab->table);
	if (ret == 0) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	ep->data = NULL;

	if (ep->key != e.key) {
		free(e.key);
	} else {
		ret = htab_keys_insert(htab, e.key);
		if (ret < 0)
			free(e.key);
	}

	return ret;
}

int htab_delete(struct htab *htab, const char *key)
{
	return htab_enter(htab, key, NULL);
}

int htab_init(size_t nel, struct htab **htab)
{
	int ret;
	struct htab *h;

	h = malloc(sizeof(*h));
	if (h == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	h->table = calloc(1, sizeof(*h->table));
	if (h == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_h;
	}

	ret = hcreate_r(nel, h->table);
	if (ret == 0) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_table;
	}
	ret = 0;

	h->keys = NULL;
	h->keys_size = 0;
	h->key_filled = 0;

out_free_table:
	if (ret < 0)
		free(h->table);
out_free_h:
	if (ret < 0)
		free(h);
	else
		*htab = h;

	return ret;
}

void htab_free(struct htab *htab)
{
	for (size_t i = 0; i < htab->key_filled; i++)
		free(htab->keys[i]);
	free(htab->keys);

	hdestroy_r(htab->table);
	free(htab->table);

	free(htab);
}
