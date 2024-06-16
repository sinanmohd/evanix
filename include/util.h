#include <stdio.h>

#include <cjson/cJSON.h>

#define print_err(fmt, ...)                                                    \
	fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define LIST_FOREACH_FREE(cur, next, head, field, func_free)                   \
	for ((cur) = ((head)->lh_first); (cur);) {                             \
		(next) = ((cur)->field.le_next);                               \
		func_free((cur));                                              \
		(cur) = (next);                                                \
	}

#define CIRCLEQ_FOREACH_FREE(cur, next, head, field, func_free)                \
	for ((cur) = ((head)->cqh_first); (cur) != (const void *)(head);) {    \
		(next) = ((cur)->field.cqe_next);                              \
		func_free((cur));                                              \
		(cur) = (next);                                                \
	}

int json_streaming_read(FILE *stream, cJSON **json);
int vpopen(FILE **stream, const char *file, char *const argv[]);
