#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>

#include "jobs.h"

#ifndef EVANIX_H

struct statistics {
	struct sqlite3 *db;
	sqlite3_stmt *statement;
};

struct evanix_opts_t {
	bool isflake;
	bool isdryrun;
	bool ispipelined;
	bool solver_report;
	bool close_unused_fd;
	bool check_cache_status;
	bool break_evanix;
	char *system;
	struct statistics statistics;
	uint32_t max_builds;
	uint32_t max_time;
	int (*solver)(struct job **, struct job_clist *, int32_t);
};

extern struct evanix_opts_t evanix_opts;

#define EVANIX_H
#endif
