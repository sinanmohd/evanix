#include <pthread.h>
#include <search.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/queue.h>

#include "htab.h"
#include "jobs.h"
#include "solver_util.h"

#ifndef QUEUE_H

typedef enum {
	Q_ITS_OVER = 0,
	Q_SEM_WAIT = 1,
} queue_state_t;

struct queue {
	struct job_clist jobs;
	sem_t sem;
	queue_state_t state;
	pthread_mutex_t mutex;
	struct htab *htab;

	/* solver */
	struct jobid *jobid;
	int32_t resources;
};

struct queue_thread {
	pthread_t tid;
	struct queue *queue;
	FILE *stream;
};

int queue_thread_new(struct queue_thread **queue_thread, FILE *stream);
void queue_thread_free(struct queue_thread *queue_thread);
void *queue_thread_entry(void *queue_thread);
int queue_pop(struct queue *queue, struct job **job, struct htab *htab);
int queue_isempty(struct job_clist *jobs);

#define QUEUE_H
#endif
