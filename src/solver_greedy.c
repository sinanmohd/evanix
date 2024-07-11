#include <errno.h>
#include <queue.h>

#include "evanix.h"
#include "jobs.h"
#include "queue.h"
#include "solver_greedy.h"
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

static void solver_report(struct job *job, int32_t max_build)
{
	if (!evanix_opts.solver_report)
		return;
	else if (job->reported)
		return;

	job->reported = true;
	printf("❌ cost: %2d > %2d <-> %s -> %s\n", job_cost(job), max_build,
	       job->name, job->drv_path);

	for (size_t i = 0; i < job->parents_filled; i++)
		solver_report(job->parents[i], max_build);
}

int solver_greedy(struct job_clist *q, int32_t *max_build, struct job **job)
{
	struct job *j;
	float conformity_cur;

	struct job *selected = NULL;
	float conformity_max = -1;

	CIRCLEQ_FOREACH (j, q, clist) {
		if (j->stale) {
			continue;
		} else if (job_cost(j) > *max_build) {
			job_stale_set(j);
			solver_report(j, *max_build);
			continue;
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

		if (!evanix_opts.solver_report)
			continue;
		printf("ℹ️ cost: %2d, conformity: %.2f -> %s\n",
		       job_cost(j), conformity_cur, j->drv_path);
	}

	if (selected == NULL)
		return -ESRCH;

	*max_build -= job_cost(selected);
	*job = selected;
	return 0;
}
