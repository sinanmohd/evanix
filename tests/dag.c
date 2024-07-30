#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "util.h"
#include "queue.h"
#include "evanix.h"
#include "solver_sjf.h"
#include "test.h"

/*
 *  A               C	  A     C
 *   \  +     +   /   =    \  /
 *   B     B    B           B
 */

struct evanix_opts_t evanix_opts = {
	.close_unused_fd = false,
	.isflake = false,
	.ispipelined = true,
	.isdryrun = false,
	.max_build = 0,
	.system = NULL,
	.solver_report = false,
	.check_cache_status = false,
	.solver = solver_sjf,
	.break_evanix = false,
};

static void test_merge()
{
	FILE *stream;
	struct job *job, *a, *b, *c;
	struct queue_thread *qt;
	int ret;

	stream = fopen("../tests/dag_merge.json", "r");
	test_assert(stream != NULL);

	ret = queue_thread_new(&qt, stream);
	if (ret < 0)
		goto out_free_stram;


	/* A */
	ret = job_read(qt->stream, &job);
	test_assert(ret == JOB_READ_SUCCESS);
	ret = queue_htab_job_merge(&job, &qt->queue->htab);
	test_assert(ret >= 0);
	a = job;

	/* B */
	ret = job_read(qt->stream, &job);
	test_assert(ret == JOB_READ_SUCCESS);
	ret = queue_htab_job_merge(&job, &qt->queue->htab);
	test_assert(ret >= 0);
	b = job;

	/* C */
	ret = job_read(qt->stream, &job);
	test_assert(ret == JOB_READ_SUCCESS);
	ret = queue_htab_job_merge(&job, &qt->queue->htab);
	test_assert(ret >= 0);
	c = job;

	ret = job_read(qt->stream, &job);
	test_assert(ret == JOB_READ_EOF);

	test_assert(a->deps[0] == b);
	test_assert(a->deps[0] == c->deps[0]);

out_free_stram:
	fclose(stream);
	queue_thread_free(qt);
}

int main(void)
{
	test_run(test_merge);
}
