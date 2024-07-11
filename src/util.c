#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "evanix.h"
#include "util.h"

int json_streaming_read(FILE *stream, cJSON **json)
{
	size_t n;
	int ret;
	char *line = NULL;

	errno = 0;
	ret = getline(&line, &n, stream);
	if (ret < 0) {
		if (errno != 0) {
			print_err("%s", strerror(errno));
			ret = -errno;
		}
		ret = -EOF;

		goto out_free_line;
	}

	*json = cJSON_Parse(line);
	if (cJSON_IsInvalid(*json)) {
		print_err("%s", "Invalid JSON");
		ret = -EPERM;
		goto out_free_line;
	}

out_free_line:
	free(line);
	return ret;
}

int vpopen(FILE **stream, const char *file, char *const argv[], vpopen_t type)
{
	int fd[2], ret;
	int nullfd = -1;

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
	if (type == VPOPEN_STDOUT)
		ret = dup2(fd[1], STDOUT_FILENO);
	else
		ret = dup2(fd[1], STDERR_FILENO);
	if (ret < 0) {
		print_err("%s", strerror(errno));
		goto out_close_fd_1;
	}

	if (evanix_opts.close_unused_fd) {
		nullfd = open("/dev/null", O_WRONLY);
		if (nullfd < 0) {
			print_err("%s", strerror(errno));
			goto out_close_fd_1;
		}
		if (type == VPOPEN_STDOUT)
			ret = dup2(nullfd, STDERR_FILENO);
		else
			ret = dup2(nullfd, STDOUT_FILENO);
		if (ret < 0) {
			print_err("%s", strerror(errno));
			goto out_close_nullfd;
		}
	}

	execvp(file, argv);
	print_err("%s", strerror(errno));

out_close_nullfd:
	if (nullfd >= 0)
		close(nullfd);
out_close_fd_1:
	close(fd[1]);
	exit(EXIT_FAILURE);
}

int atob(const char *s)
{
	if (!strcmp(s, "true") || !strcmp(s, "yes") || !strcmp(s, "y"))
		return true;
	else if (!strcmp(s, "false") || !strcmp(s, "no") || !strcmp(s, "n"))
		return false;

	return -1;
}

int run(const char *file, char *argv[])
{
	int ret, wstatus;

	ret = fork();
	switch (ret) {
	case -1:
		print_err("%s", strerror(errno));
		return -errno;
	case 0:
		execvp(file, argv);
		print_err("%s", strerror(errno));
		exit(EXIT_FAILURE);
	default:
		ret = waitpid(ret, &wstatus, 0);
		if (!WIFEXITED(wstatus))
			return -EPERM;
		return WEXITSTATUS(wstatus) == 0 ? 0 : -EPERM;
	}
}

char *trim(char *s)
{
	size_t end, i;

	while (isspace(*s))
		s++;

	for (i = 0, end = 0; s[i]; i++) {
		if (isgraph(s[i]))
			end = i + 1;
	}
	s[end] = '\0';

	return s;
}
