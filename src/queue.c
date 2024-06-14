#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "queue.h"
#include "util.h"

static void queue_push(struct queue *queue, struct job *job);

void *queue_thread_entry(void *queue_thread)
{
	struct queue_thread *qt = queue_thread;
	struct job *job = NULL;
	int ret = 0;

	while (true) {
		ret = job_read(qt->stream, &job);
		if (ret == JOB_READ_EOF) {
			qt->queue->state = Q_ITS_OVER;
			sem_post(&qt->queue->sem);

			ret = 0;
			break;
		} else if (ret == JOB_READ_EVAL_ERR ||
			   ret == JOB_READ_JSON_INVAL) {
			continue;
		} else if (ret == JOB_READ_SUCCESS) {
			queue_push(qt->queue, job);
		} else {
			break;
		}
	}

	pthread_exit(NULL);
}

int queue_pop(struct queue *queue, struct job **job)
{
	struct job *j = CIRCLEQ_FIRST(&queue->jobs);

	if (CIRCLEQ_EMPTY(&queue->jobs)) {
		print_err("%s", "Empty queue");
		return -EPERM;
	}

	pthread_mutex_lock(&queue->mutex);
	CIRCLEQ_REMOVE(&queue->jobs, j, clist);
	pthread_mutex_unlock(&queue->mutex);

	*job = j;
	return 0;
}

static void queue_push(struct queue *queue, struct job *job)
{
	pthread_mutex_lock(&queue->mutex);
	CIRCLEQ_INSERT_TAIL(&queue->jobs, job, clist);
	pthread_mutex_unlock(&queue->mutex);

	sem_post(&queue->sem);
}

void queue_thread_free(struct queue_thread *queue_thread)
{
	struct job *job;
	int ret;

	if (queue_thread == NULL)
		return;

	CIRCLEQ_FOREACH (job, &queue_thread->queue->jobs, clist) {
		CIRCLEQ_REMOVE(&queue_thread->queue->jobs, job, clist);
		free(job);
	}

	ret = sem_destroy(&queue_thread->queue->sem);
	if (ret < 0)
		print_err("%s", strerror(errno));
	ret = pthread_mutex_destroy(&queue_thread->queue->mutex);
	if (ret < 0)
		print_err("%s", strerror(errno));

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
	qt->queue->state = Q_SEM_WAIT;
	ret = sem_init(&qt->queue->sem, 0, 0);
	if (ret < 0) {
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
