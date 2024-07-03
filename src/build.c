#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "evanix.h"
#include "jobs.h"
#include "queue.h"
#include "util.h"

static int build(struct queue *queue);

void *build_thread_entry(void *build_thread)
{
	struct build_thread *bt = build_thread;
	int ret = 0;

	while (true) {
		ret = sem_wait(&bt->queue->sem);
		if (ret < 0) {
			print_err("%s", strerror(errno));
			goto out;
		}

		if (queue_isempty(&bt->queue->jobs)) {
			if (bt->queue->state == Q_ITS_OVER)
				goto out;
			else if (bt->queue->state == Q_SEM_WAIT)
				continue;
		}

		ret = build(bt->queue);
		if (ret < 0)
			goto out;
	}

out:
	pthread_exit(NULL);
}

static int build(struct queue *queue)
{
	struct job *job;
	char *args[5];
	size_t argindex;
	int ret;

	char out_link[NAME_MAX] = "result";

	ret = queue_pop(queue, &job, queue->htab);
	if (ret == -ESRCH)
		return EAGAIN;
	if (ret < 0)
		return ret;

	if (job->nix_attr_name) {
		ret = snprintf(out_link, sizeof(out_link), "result-%s",
			       job->nix_attr_name);
		if (ret < 0 || (size_t)ret > sizeof(out_link)) {
			ret = -ENAMETOOLONG;
			print_err("%s", strerror(-ret));
			goto out_free_job;
		}
	}

	argindex = 0;
	args[argindex++] = "nix-build";
	args[argindex++] = "--out-link";
	args[argindex++] = out_link;
	args[argindex++] = job->drv_path;
	args[argindex++] = NULL;

	if (evanix_opts.isdryrun) {
		for (size_t i = 0; i < argindex - 1; i++)
			printf("%s%c", args[i],
			       (i + 2 == argindex) ? '\n' : ' ');
	} else {
		run("nix-build", args);
	}

out_free_job:
	job_free(job);

	return ret;
}

int build_thread_new(struct build_thread **build_thread, struct queue *q)
{
	struct build_thread *bt = NULL;

	bt = malloc(sizeof(*bt));
	if (bt == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	bt->queue = q;

	*build_thread = bt;
	return 0;
}
