#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "queue.h"
#include "util.h"

void *queue_thread_entry(void *queue_thread)
{
	struct queue_thread *qt = queue_thread;
	struct job *job = NULL;
	int ret = 0;

	while (true) {
		ret = job_read(qt->stream, &job);
		if (ret == -EOF) {
			ret = 0;
			break;
		} else if (ret < 0) {
			break;
		}

		pthread_mutex_lock(&qt->queue->mutex);
		CIRCLEQ_INSERT_TAIL(&qt->queue->jobs, job, clist);
		pthread_mutex_unlock(&qt->queue->mutex);
	}

	pthread_exit(NULL);
}

void queue_thread_free(struct queue_thread *queue_thread)
{
	struct job *job;

	if (queue_thread == NULL)
		return;

	CIRCLEQ_FOREACH (job, &queue_thread->queue->jobs, clist) {
		CIRCLEQ_REMOVE(&queue_thread->queue->jobs, job, clist);
		free(job);
	}

	free(queue_thread->queue);
	fclose(queue_thread->stream);
}

int queue_thread_new(struct queue_thread **queue_thread, FILE *stream)
{
	int ret = 0;
	struct queue_thread *qt = NULL;

	qt = malloc(sizeof(*qt));
	if (qt == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	qt->stream = stream;

	qt->queue = malloc(sizeof(*qt->queue));
	if (qt->queue == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free;
	}
	CIRCLEQ_INIT(&qt->queue->jobs);
	pthread_mutex_init(&qt->queue->mutex, NULL);

out_free:
	if (ret < 0)
		free(qt);
	else
		*queue_thread = qt;

	return ret;
}
