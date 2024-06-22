#include <search.h>

#ifndef HTAB_H

struct htab {
	struct hsearch_data *table;
	char **keys;
	size_t keys_size, key_filled;
};

void htab_free(struct htab *htab);
int htab_init(struct htab **htab);
int htab_delete(struct htab *htab, const char *key);
int htab_enter(struct htab *htab, const char *key, void *data);
int htab_search(struct htab *htab, char *key, ENTRY **ep);

#define HTAB_H
#endif
