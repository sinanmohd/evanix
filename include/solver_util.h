#include <stdint.h>

#include "jobs.h"

#ifndef SOLVER_UTIL_H

struct jobid {
	struct job **jobs;
	size_t filled, size;
	uint32_t *cost;
	/* user directly asked for this to be built, not a transitively acquired
	 * dependency */
	bool *isdirect;
};

void jobid_free(struct jobid *jid);
int jobid_init(struct job_clist *q, struct jobid **job_ids);
int solver_fcfs(struct job **job, struct job_clist *q,
		__attribute__((unused)) int32_t resources);

#define SOLVER_UTIL_H
#endif
