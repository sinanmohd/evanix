#include <errno.h>
#include <queue.h>

#include "evanix.h"
#include "jobs.h"
#include "queue.h"
#include "solver_conformity.h"
#include "util.h"

static float conformity(struct job *job);

/* conformity is a ratio between number of direct feasible derivations sharing
 * dependencies of a derivation and total number of dependencies */
static float conformity(struct job *job)
{
	float conformity = 0;

	if (job->deps_filled == 0)
		return 0;

	for (size_t i = 0; i < job->deps_filled; i++) {
		for (size_t j = 0; j < job->deps[i]->parents_filled; j++) {
			/* don't count the job itself */
			if (job->deps[i]->parents[j] == job)
				continue;
			/* don't count stale parents */
			if (job->deps[i]->parents[j]->stale)
				continue;

			conformity++;
		}
	}
	conformity /= job->deps_filled;

	return conformity;
}

int solver_conformity(struct job **job, struct job_clist *q, int32_t resources)
{
	struct job *j;
	float conformity_cur;
	int ret;

	struct job *selected = NULL;
	float conformity_max = -1;

	CIRCLEQ_FOREACH (j, q, clist) {
		if (j->stale)
			continue;

		ret = job_cost_recursive(j);
		if (ret < 0)
			return ret;

		if (ret > resources) {
			job_stale_set(j);
			if (evanix_opts.solver_report) {
				printf("❌ refusing to build %s, cost: %d\n",
				       j->drv_path, ret);
			}
		}
	}

	CIRCLEQ_FOREACH (j, q, clist) {
		if (j->stale)
			continue;

		conformity_cur = conformity(j);
		if (conformity_cur > conformity_max) {
			conformity_max = conformity_cur;
			selected = j;
		} else if (conformity_cur == conformity_max &&
			   selected->deps_filled > j->deps_filled) {
			selected = j;
		}
	}

	if (selected == NULL)
		return -ESRCH;

	*job = selected;
	return job_cost_recursive(selected);
}
