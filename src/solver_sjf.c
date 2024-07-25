#include <queue.h>
#include <errno.h>

#include "evanix.h"
#include "jobs.h"
#include "solver_sjf.h"

int solver_sjf(struct job **job, struct job_clist *q, int32_t resources)
{
	struct job *j;
	int cost_cur;

	struct job *selected = NULL;
	int cost_min = -1;

	CIRCLEQ_FOREACH (j, q, clist) {
		if (j->stale)
			continue;

		cost_cur = job_cost_recursive(j);
		if (cost_cur > resources) {
			job_stale_set(j);
			if (evanix_opts.solver_report) {
				printf("❌ refusing to build %s, cost: %d\n",
				       j->drv_path, job_cost_recursive(j));
			}
		}

		if (cost_min < 0 || cost_min > cost_cur) {
			selected = j;
			cost_min = cost_cur;
		}
	}

	*job = selected;
	return (cost_min < 0) ? -ESRCH : cost_min;
}
