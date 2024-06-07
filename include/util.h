#include <stdio.h>

#include <cjson/cJSON.h>

#define print_err(fmt, ...)                                                    \
	fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

int json_streaming_read(FILE *stream, cJSON **json);
int vpopen(FILE **stream, const char *file, char *const argv[]);
