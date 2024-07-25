#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "evanix.h"
#include "queue.h"
#include "solver_conformity.h"
#include "util.h"

#define MAX_NIX_PKG_COUNT 200000

static int queue_push(struct queue *queue, struct job *job);
static int queue_htab_job_merge(struct job **job, struct job **htab);
static int queue_dag_isolate(struct job *job, struct job *keep_parent,
			     struct job_clist *jobs, struct job **htab);

static int queue_dag_isolate(struct job *job, struct job *keep_parent,
			     struct job_clist *jobs, struct job **htab)
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

	if (job->scheduled)
		CIRCLEQ_REMOVE(jobs, job, clist);

	HASH_DEL(*htab, job);

	return 0;
}

int queue_isempty(struct job_clist *jobs)
{
	struct job *j;

	CIRCLEQ_FOREACH (j, jobs, clist) {
		if (j->stale == false)
			return false;
	}

	return true;
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

int queue_pop(struct queue *queue, struct job **job)
{
	int ret;
	struct job *j;

	if (CIRCLEQ_EMPTY(&queue->jobs)) {
		print_err("%s", "Empty queue");
		return -EPERM;
	}

	pthread_mutex_lock(&queue->mutex);
	if (evanix_opts.max_build) {
		ret = evanix_opts.solver(&j, &queue->jobs, queue->resources);
		if (ret < 0)
			goto out_mutex_unlock;
		queue->resources -= ret;
	} else {
		j = CIRCLEQ_FIRST(&queue->jobs);
	}
	ret = queue_dag_isolate(j, NULL, &queue->jobs, &queue->htab);
	if (ret < 0)
		goto out_mutex_unlock;

out_mutex_unlock:
	pthread_mutex_unlock(&queue->mutex);

	if (ret >= 0)
		*job = j;
	return ret;
}

/* this merge functions are closely tied to the output characteristics of
 * nix-eval-jobs, that is
 * - only two level of nodes (root and childrens or dependencies)
 * - only childrens or dependencies have parent node
 * - only root node have dependencies
 */
static int queue_htab_job_merge(struct job **job, struct job **htab)
{
	int ret;
	struct job *jtab = NULL;
	struct job *j = *job;

	HASH_FIND_STR(*htab, j->drv_path, jtab);
	if (jtab == NULL) {
		HASH_ADD_STR(*htab, drv_path, j);

		for (size_t i = 0; i < j->deps_filled; i++) {
			ret = queue_htab_job_merge(&j->deps[i], htab);
			if (ret < 0)
				return ret;
		}

		return 0;
	}

	/* if it's already inside htab, it's deps should also be in htab, hence
	 * not merging deps */
	if (jtab->name == NULL) {
		/* steal name from new job struct */
		jtab->name = j->name;
		j->name = NULL;
	}

	/* only recursive calls with childrens or dependencies can enter this
	 * for a recursive call to happen the parent was just entered into htab
	 * so,
	 * - update parent's reference to point to jtab (done by *job = jtab)
	 * - insert the parent to jtab
	 */
	if (j->parents_filled > 0) {
		ret = job_parents_list_insert(jtab, j->parents[0]);
		if (ret < 0)
			return ret;
		j->parents_filled = 0;
	}

	job_free(*job);
	*job = jtab;
	return 0;
}

static int queue_push(struct queue *queue, struct job *job)
{
	int ret;

	pthread_mutex_lock(&queue->mutex);
	ret = queue_htab_job_merge(&job, &queue->htab);
	if (ret < 0) {
		pthread_mutex_unlock(&queue->mutex);
		return ret;
	}

	/* no duplicate entries in queue */
	if (!job->scheduled) {
		job->scheduled = true;
		CIRCLEQ_INSERT_TAIL(&queue->jobs, job, clist);
	}
	pthread_mutex_unlock(&queue->mutex);
	sem_post(&queue->sem);

	return 0;
}

void queue_thread_free(struct queue_thread *queue_thread)
{
	struct job *j;
	int ret;

	if (queue_thread == NULL)
		return;

	while (!CIRCLEQ_EMPTY(&queue_thread->queue->jobs)) {
		j = CIRCLEQ_FIRST(&queue_thread->queue->jobs);
		ret = queue_dag_isolate(j, NULL, &queue_thread->queue->jobs,
					&queue_thread->queue->htab);
		if (ret < 0)
			return;
		job_free(j);
	}

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
	qt->queue->htab = NULL;
	qt->queue->resources = evanix_opts.max_build;
	qt->queue->jobid = NULL;
	qt->queue->state = Q_SEM_WAIT;
	ret = sem_init(&qt->queue->sem, 0, 0);
	if (ret < 0) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_queue;
	}

	CIRCLEQ_INIT(&qt->queue->jobs);
	pthread_mutex_init(&qt->queue->mutex, NULL);

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
