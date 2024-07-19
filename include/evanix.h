#include <stdbool.h>
#include <stdint.h>

#ifndef EVANIX_H

struct evanix_opts_t {
	bool isflake;
	bool isdryrun;
	bool ispipelined;
	bool solver_report;
	bool close_unused_fd;
	bool cache_status;
	char *system;
	uint32_t max_build;
};

extern struct evanix_opts_t evanix_opts;

#define EVANIX_H
#endif
