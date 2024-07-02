#include <errno.h>
#include <queue.h>
#include <stdlib.h>
#include <string.h>

#include "jobs.h"
#include "solver_util.h"
#include "util.h"

static int dag_id_assign(struct job *j, struct jobid *jobid);

static int dag_id_assign(struct job *j, struct jobid *jobid)
{
	size_t newsize;
	void *ret;

	if (j->id >= 0)
		return 0;

	for (size_t i = 0; i < j->deps_filled; i++)
		return dag_id_assign(j->deps[i], jobid);

	if (jobid->size < jobid->filled) {
		j->id = jobid->filled++;
		jobid->jobs[j->id] = j;
		return 0;
	}

	newsize = jobid->size == 0 ? 2 : jobid->size * 2;
	ret = realloc(jobid->jobs, newsize * sizeof(*jobid->jobs));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	jobid->jobs = ret;

	j->id = jobid->filled++;
	jobid->jobs[j->id] = j;

	return 0;
}

void jobid_free(struct jobid *jid)
{
	free(jid->cost);
	free(jid->isdirect);
	free(jid->jobs);
	free(jid);
}

int jobid_init(struct job_clist *q, struct jobid **job_ids)
{
	struct jobid *jid;
	struct job *j;
	int ret;

	jid = malloc(sizeof(*jid));
	if (jid == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	jid->jobs = NULL;
	jid->cost = NULL;
	jid->isdirect = NULL;
	jid->size = 0;
	jid->filled = 0;

	CIRCLEQ_FOREACH (j, q, clist) {
		ret = dag_id_assign(j, jid);
		if (ret < 0) {
			goto out_free_jid;
		}
	}

	jid->isdirect = malloc(jid->filled * sizeof(*jid->isdirect));
	if (jid->isdirect == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	jid->cost = malloc(jid->filled * sizeof(*jid->cost));
	if (jid->cost == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	for (size_t i = 0; i < jid->filled; i++) {
		jid->isdirect[i] = jid->jobs[i]->scheduled;
		jid->cost[i] = 1;
	}

out_free_jid:
	if (ret < 0) {
		free(jid->cost);
		free(jid->isdirect);
		free(jid->jobs);
		free(jid);
	} else {
		*job_ids = jid;
	}

	return ret;
}
