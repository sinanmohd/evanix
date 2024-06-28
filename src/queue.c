#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "queue.h"
#include "util.h"

#define MAX_NIX_PKG_COUNT 200000

static int queue_push(struct queue *queue, struct job *job);
static int queue_htab_job_merge(struct job **job, struct htab *htab);
static int queue_dag_isolate(struct job *job, struct job *keep_parent,
			     struct job_clist *jobs, struct htab *htab);

static int queue_dag_isolate(struct job *job, struct job *keep_parent,
			     struct job_clist *jobs, struct htab *htab)
{
	int ret;

	for (size_t i = 0; i < job->deps_filled; i++) {
		ret = queue_dag_isolate(job->deps[i], job, jobs, htab);
		if (ret < 0)
			return ret;
	}

	for (size_t i = 0; i < job->parents_filled; i++) {
		if (job->parents[i] == keep_parent)
			continue;

		job_deps_list_rm(job->parents[i], job);
	}

	if (keep_parent != NULL) {
		job->parents[0] = keep_parent;
		job->parents_filled = 1;
	} else {
		/* it must be tha parent */
		job->parents_filled = 0;
	}

	if (!job->transitive)
		CIRCLEQ_REMOVE(jobs, job, clist);

	ret = htab_delete(htab, job->drv_path);
	if (ret < 0)
		return ret;

	return 0;
}

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
			   ret == JOB_READ_JSON_INVAL ||
			   ret == JOB_READ_SYS_MISMATCH ||
			   ret == JOB_READ_CACHED) {
			continue;
		} else if (ret == JOB_READ_SUCCESS) {
			queue_push(qt->queue, job);
		} else {
			break;
		}
	}

	pthread_exit(NULL);
}

int queue_pop(struct queue *queue, struct job **job, struct htab *htab)
{
	int ret;

	struct job *j = CIRCLEQ_FIRST(&queue->jobs);

	if (CIRCLEQ_EMPTY(&queue->jobs)) {
		print_err("%s", "Empty queue");
		return -EPERM;
	}

	pthread_mutex_lock(&queue->mutex);
	ret = queue_dag_isolate(j, NULL, &queue->jobs, htab);
	if (ret < 0)
		return ret;
	pthread_mutex_unlock(&queue->mutex);

	*job = j;
	return 0;
}

/* this merge functions are closely tied to the output characteristics of
 * nix-eval-jobs, that is
 * - only two level of nodes (root and childrens or dependencies)
 * - only childrens or dependencies have parent node
 * - only root node have dependencies
 */
static int queue_htab_job_merge(struct job **job, struct htab *htab)
{
	struct job *jtab;
	ENTRY *ep;
	int ret;

	ret = htab_search(htab, (*job)->drv_path, &ep);
	if (ret < 0) {
		return ret;
	} else if (ret == ESRCH) {
		ret = htab_enter(htab, (*job)->drv_path, *job);
		if (ret < 0)
			return ret;

		for (size_t i = 0; i < (*job)->deps_filled; i++) {
			ret = queue_htab_job_merge(&(*job)->deps[i], htab);
			if (ret < 0)
				return ret;
		}

		return 0;
	}

	/* if it's already inside htab, it's deps should also be in htab, hence
	 * not merging deps */
	jtab = ep->data;
	if (jtab->name == NULL) {
		/* steal name from new job struct */
		jtab->name = (*job)->name;
		(*job)->name = NULL;
	}

	/* only recursive calls with childrens or dependencies can enter this
	 * for a recursive call to happen the parent was just entered into htab
	 * so,
	 * - update parent's reference to point to jtab (done by *job = jtab)
	 * - insert the parent to jtab
	 */
	if ((*job)->parents_filled > 0) {
		ret = job_parents_list_insert(jtab, (*job)->parents[0]);
		if (ret < 0)
			return ret;
		(*job)->parents_filled = 0;
	}

	job_free(*job);
	*job = jtab;
	return 0;
}

static int queue_push(struct queue *queue, struct job *job)
{
	int ret;

	pthread_mutex_lock(&queue->mutex);
	ret = queue_htab_job_merge(&job, queue->htab);
	if (ret < 0) {
		pthread_mutex_unlock(&queue->mutex);
		return ret;
	}

	/* no duplicate entries in queue */
	if (job->transitive) {
		job->transitive = false;
		CIRCLEQ_INSERT_TAIL(&queue->jobs, job, clist);
	}
	pthread_mutex_unlock(&queue->mutex);
	sem_post(&queue->sem);

	return 0;
}

void queue_thread_free(struct queue_thread *queue_thread)
{
	struct job *cur, *next;
	int ret;

	if (queue_thread == NULL)
		return;

	CIRCLEQ_FOREACH_FREE(cur, next, &queue_thread->queue->jobs, clist,
			     job_free);

	htab_free(queue_thread->queue->htab);
	ret = sem_destroy(&queue_thread->queue->sem);
	if (ret < 0)
		print_err("%s", strerror(errno));
	ret = pthread_mutex_destroy(&queue_thread->queue->mutex);
	if (ret < 0)
		print_err("%s", strerror(errno));

	free(queue_thread->queue);
	fclose(queue_thread->stream);
	free(queue_thread);
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
		goto out_free_qt;
	}
	qt->queue->state = Q_SEM_WAIT;
	ret = sem_init(&qt->queue->sem, 0, 0);
	if (ret < 0) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_queue;
	}

	ret = htab_init(MAX_NIX_PKG_COUNT, &qt->queue->htab);
	if (ret < 0)
		goto out_free_sem;

	CIRCLEQ_INIT(&qt->queue->jobs);
	pthread_mutex_init(&qt->queue->mutex, NULL);

out_free_sem:
	if (ret < 0)
		sem_destroy(&qt->queue->sem);
out_free_queue:
	if (ret < 0)
		free(qt->queue);
out_free_qt:
	if (ret < 0)
		free(qt);
	else
		*queue_thread = qt;

	return ret;
}
