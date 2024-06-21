#include <pthread.h>
#include <search.h>
#include <semaphore.h>
#include <sys/queue.h>

#include "jobs.h"

#ifndef QUEUE_H

typedef enum {
	Q_ITS_OVER = 0,
	Q_SEM_WAIT = 1,
} queue_state_t;

struct queue {
	struct job_clist jobs;
	struct hsearch_data *htab;
	sem_t sem;
	queue_state_t state;
	pthread_mutex_t mutex;
};

struct queue_thread {
	pthread_t tid;
	struct queue *queue;
	FILE *stream;
};

int queue_thread_new(struct queue_thread **queue_thread, FILE *stream);
void queue_thread_free(struct queue_thread *queue_thread);
void *queue_thread_entry(void *queue_thread);
int queue_pop(struct queue *queue, struct job **job, struct hsearch_data *htab);

#define QUEUE_H
#endif
