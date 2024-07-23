#include <stdint.h>

#include "jobs.h"

#ifndef SOLVER_UTIL_H

struct jobid {
	struct job **jobs;
	size_t filled, size;
};

void jobid_free(struct jobid *jid);
int jobid_init(struct job_clist *q, struct jobid **jobid);

#define SOLVER_UTIL_H
#endif
