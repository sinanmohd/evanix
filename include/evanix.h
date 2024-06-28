#include <stdbool.h>

#ifndef EVANIX_H

struct evanix_opts_t {
	bool isflake;
	bool isdryrun;
	bool ispipelined;
	bool close_stderr_exec;
	char *system;
};

extern struct evanix_opts_t evanix_opts;

#define EVANIX_H
#endif
