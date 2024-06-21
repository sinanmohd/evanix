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
static int queue_htab_job_merge(struct job **job, struct hsearch_data *htab);
static int queue_htab_parent_merge(struct job *to, struct job *from);

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

int queue_pop(struct queue *queue, struct job **job, struct hsearch_data *htab)
{
	ENTRY e, *ep;
	int ret;

	struct job *j = CIRCLEQ_FIRST(&queue->jobs);

	if (CIRCLEQ_EMPTY(&queue->jobs)) {
		print_err("%s", "Empty queue");
		return -EPERM;
	}

	pthread_mutex_lock(&queue->mutex);

	CIRCLEQ_REMOVE(&queue->jobs, j, clist);
	if (j->parents_filled <= 0) {
		e.key = j->drv_path;
		ret = hsearch_r(e, FIND, &ep, htab);
		if (ret == 0) {
			print_err("%s", strerror(errno));
		}
		else
			ep->data = NULL;
	}

	pthread_mutex_unlock(&queue->mutex);

	*job = j;
	return 0;
}

static int queue_htab_parent_merge(struct job *to, struct job *from)
{
	int ret;

	/* the output from nix-eval-jobs("from") can only have maximum one
	 * parent */
	for (size_t i = 0; i < to->parents_filled; i++) {
		if (strcmp(to->parents[i]->drv_path,
			   from->parents[0]->drv_path))
			continue;

		/* steal name from "from" */
		if (to->parents[i]->name == NULL) {
			to->parents[i]->name =
				from->parents[0]->name;
			from->parents[0]->name = NULL;
		}

		return 0;
	}

	ret = job_parents_list_insert(to, from->parents[0]);
	if (ret < 0)
		return ret;
	from->parents_filled = 0;

	return 0;
}

/* this merge functions are closely tied to the output characteristics of
 * nix-eval-jobs, that is
 * - only two level of nodes (root and childrens or dependencies)
 * - only childrens or dependencies have parent node
 * - only root node have dependencies
 */
static int queue_htab_job_merge(struct job **job, struct hsearch_data *htab)
{
	struct job *jtab;
	ENTRY e, *ep;
	int ret;

	e.key = (*job)->drv_path;
	ret = hsearch_r(e, FIND, &ep, htab);
	if (ret == 0) {
		if (errno != ESRCH) {
			print_err("%s", strerror(errno));
			return -errno;
		}

		e.data = *job;
		ret = hsearch_r(e, ENTER, &ep, htab);
		if (ret == 0) {
			print_err("%s", strerror(errno));
			return -errno;
		}

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
	 * so, update the parent's deps reference to point to the node in htab
	 */
	if ((*job)->parents_filled > 0) {
		ret = queue_htab_parent_merge(jtab, *job);
		if (ret < 0)
			return ret;
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

	hdestroy_r(queue_thread->queue->htab);
	free(queue_thread->queue->htab);
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

	qt->queue->htab = malloc(sizeof(*qt->queue->htab));
	if (qt->queue->htab == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_sem;
	}
	ret = hcreate_r(MAX_NIX_PKG_COUNT, qt->queue->htab);
	if (ret == 0) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_htab;
	}

	CIRCLEQ_INIT(&qt->queue->jobs);
	pthread_mutex_init(&qt->queue->mutex, NULL);

out_free_htab:
	if (ret < 0)
		free(qt->queue->htab);
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
