#include <errno.h>
#include <highs/interfaces/highs_c_api.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "evanix.h"
#include "jobid.h"
#include "solver_highs.h"
#include "util.h"

static int solver_highs_unwrapped(double *solution, struct job_clist *q,
				  int32_t resources, struct jobid *jobid)
{
	HighsInt precedence_index[2];
	double precedence_value[2];
	int num_non_zero;
	struct job *j;
	int ret;

	double *col_profit = NULL;
	double *col_lower = NULL;
	double *col_upper = NULL;
	HighsInt *integrality = NULL;
	void *highs = NULL;

	HighsInt *constraint_index = NULL;
	double *constraint_value = NULL;

	/* set objective */
	col_profit = calloc(jobid->filled, sizeof(*col_profit));
	if (col_profit == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	for (size_t i = 0; i < jobid->filled; i++) {
		if (jobid->jobs[i]->requested)
			col_profit[i] = 1.0;
	}

	col_lower = calloc(jobid->filled, sizeof(*col_lower));
	if (col_lower == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_col_profit;
	}

	col_upper = malloc(jobid->filled * sizeof(*col_lower));
	if (col_upper == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_col_profit;
	}
	for (size_t i = 0; i < jobid->filled; i++)
		col_upper[i] = 1.0;

	highs = Highs_create();

	if (evanix_opts.solver_report)
		ret = Highs_setBoolOptionValue(highs, "output_flag", 1);
	else
		ret = Highs_setBoolOptionValue(highs, "output_flag", 0);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}

	ret = Highs_addCols(highs, jobid->filled, col_profit, col_lower,
			    col_upper, 0, NULL, NULL, NULL);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}

	/* set resource constraint */
	constraint_index = malloc(jobid->filled * sizeof(*constraint_index));
	if (constraint_index == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_col_profit;
	}
	constraint_value = malloc(jobid->filled * sizeof(*constraint_value));
	if (constraint_value == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_col_profit;
	}

	num_non_zero = 0;
	for (size_t i = 0; i < jobid->filled; i++) {
		ret = job_cost(jobid->jobs[i]);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			continue;

		constraint_value[num_non_zero] = ret;
		constraint_index[num_non_zero] = i;
		num_non_zero++;
	}

	ret = Highs_addRow(highs, 0, resources, num_non_zero, constraint_index,
			   constraint_value);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}

	/* set precedance constraints */
	CIRCLEQ_FOREACH (j, q, clist) {
		for (size_t i = 0; i < j->deps_filled; i++) {
			/* follow the CSR matrix structure */
			if (j->id < j->deps[i]->id) {
				precedence_index[0] = j->id;
				precedence_index[1] = j->deps[i]->id;
				precedence_value[0] = 1;
				precedence_value[1] = -1;
			} else {
				precedence_index[0] = j->deps[i]->id;
				precedence_index[1] = j->id;
				precedence_value[0] = -1;
				precedence_value[1] = 1;
			}

			ret = Highs_addRow(highs, -INFINITY, 0, 2,
					   precedence_index, precedence_value);
		}
	}

	/* run milp solver */
	ret = Highs_changeObjectiveSense(highs, kHighsObjSenseMaximize);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}

	integrality = malloc(jobid->filled * sizeof(*integrality));
	if (integrality == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_col_profit;
	}
	for (size_t i = 0; i < jobid->filled; i++)
		integrality[i] = 1;
	ret = Highs_changeColsIntegralityByMask(highs, integrality,
						integrality);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}

	ret = Highs_run(highs);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}
	ret = Highs_getSolution(highs, solution, NULL, NULL, NULL);
	if (ret != kHighsStatusOk) {
		print_err("%s", "highs did not return kHighsStatusOk");
		ret = -EPERM;
		goto out_free_col_profit;
	}
	Highs_destroy(highs);

out_free_col_profit:
	free(col_profit);
	free(col_lower);
	free(col_upper);
	free(integrality);
	free(constraint_value);
	free(constraint_index);

	return ret;
}

static int job_get(struct job **job, struct job_clist *q)
{
	struct job *j;

	CIRCLEQ_FOREACH (j, q, clist) {
		if (j->stale)
			continue;

		*job = j;
		return job_cost_recursive(j);
	}

	print_err("%s", "empty queue");
	return -ESRCH;
}

int solver_highs(struct job **job, struct job_clist *q, int32_t resources)
{
	static bool solved = false;
	struct jobid *jobid = NULL;
	double *solution = NULL;
	struct job *j;
	int ret = 0;

	if (solved)
		goto out_free_jobid;

	ret = jobid_init(q, &jobid);
	if (ret < 0)
		return ret;

	solution = malloc(jobid->filled * sizeof(*solution));
	if (solution == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_jobid;
	}

	ret = solver_highs_unwrapped(solution, q, resources, jobid);
	if (ret < 0)
		goto out_free_jobid;

	for (size_t i = 0; i < jobid->filled; i++) {
		if (solution[i] == 0.0)
			job_stale_set(jobid->jobs[i]);
	}

	if (evanix_opts.solver_report) {
		CIRCLEQ_FOREACH (j, q, clist) {
			if (j->stale) {
				printf("❌ refusing to build %s, cost: %d\n",
				       j->drv_path, job_cost_recursive(j));
			}
		}
	}

	solved = true;
out_free_jobid:
	jobid_free(jobid);
	free(solution);

	if (ret < 0)
		return ret;
	else
		return job_get(job, q);
}
