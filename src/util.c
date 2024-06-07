#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "util.h"

int json_streaming_read(FILE *stream, cJSON **json)
{
	int ret;
	size_t n;
	char *line = NULL;

	ret = getline(&line, &n, stream);
	if (ret < 0) {
		if (errno != 0) {
			print_err("%s", strerror(errno));
			return -errno;
		}

		return -EOF;
	}

	*json = cJSON_Parse(line);
	free(line);
	if (cJSON_IsInvalid(*json)) {
		print_err("%s", "Invalid JSON");
		return -EPERM;
	}

	return 0;
}

int vpopen(FILE **stream, const char *file, char *const argv[])
{
	int fd[2];
	int ret;

	ret = pipe(fd);
	if (ret < 0) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	ret = fork();
	if (ret < 0) {
		print_err("%s", strerror(errno));

		close(fd[0]);
		close(fd[1]);
		return -errno;
	} else if (ret > 0) {
		close(fd[1]);
		*stream = fdopen(fd[0], "r");
		if (*stream == NULL) {
			print_err("%s", strerror(errno));
			return -errno;
		}

		return 0;
	}

	close(fd[0]);
	ret = dup2(fd[1], STDOUT_FILENO);
	if (ret < 0)
		goto out_err;

	execvp(file, argv);

out_err:
	close(fd[1]);
	print_err("%s", strerror(errno));
	exit(EXIT_FAILURE);
}
