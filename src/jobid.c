#include <errno.h>
#include <queue.h>
#include <stdlib.h>
#include <string.h>

#include "jobid.h"
#include "jobs.h"
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
	if (jid == NULL)
		return;

	free(jid->jobs);
	free(jid);
}

int jobid_init(struct job_clist *q, struct jobid **jobid)
{
	struct jobid *jid;
	struct job *j;
	int ret = 0;

	jid = malloc(sizeof(*jid));
	if (jid == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	jid->jobs = NULL;
	jid->size = 0;
	jid->filled = 0;

	CIRCLEQ_FOREACH (j, q, clist) {
		ret = dag_id_assign(j, jid);
		if (ret < 0) {
			goto out_free_jid;
		}
	}

out_free_jid:
	if (ret < 0) {
		free(jid->jobs);
		free(jid);
	} else {
		*jobid = jid;
	}

	return ret;
}
