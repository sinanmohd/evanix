#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
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

		if (CIRCLEQ_EMPTY(&bt->queue->jobs)) {
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
	int ret = 0;

	ret = queue_pop(queue, &job, queue->htab);
	if (ret < 0)
		return ret;

	printf("nix build %s^*\n", job->drv_path);
	job_free(job);

	return 0;
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
