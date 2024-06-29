#include <errno.h>
#include <queue.h>
#include <stdlib.h>
#include <string.h>

#include "jobs.h"
#include "solver_util.h"
#include "util.h"

static int dag_id_assign(struct job *j, struct job_ids *job_ids)
{
	size_t newsize;
	void *ret;

	if (j->id >= 0)
		return 0;

	for (size_t i = 0; i < j->deps_filled; i++)
		return dag_id_assign(j->deps[i], job_ids);

	if (job_ids->size < job_ids->filled) {
		j->id = job_ids->filled++;
		job_ids->jobs[j->id] = j;
		return 0;
	}

	newsize = job_ids->size == 0 ? 2 : job_ids->size * 2;
	ret = realloc(job_ids->jobs, newsize * sizeof(*job_ids->jobs));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	job_ids->jobs = ret;

	j->id = job_ids->filled++;
	job_ids->jobs[j->id] = j;

	return 0;
}

int queue_id_assign(struct job_clist *q, struct job_ids **job_ids)
{
	struct job_ids *ji;
	struct job *j;
	int ret;

	ji = malloc(sizeof(*ji));
	if (ji == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	ji->jobs = NULL;
	ji->size = 0;
	ji->filled = 0;

	CIRCLEQ_FOREACH (j, q, clist) {
		ret = dag_id_assign(j, ji);
		if (ret < 0) {
			goto out_free_js;
		}
	}

out_free_js:
	if (ret < 0) {
		free(ji->jobs);
		free(ji);
	} else {
		*job_ids = ji;
	}

	return ret;
}
