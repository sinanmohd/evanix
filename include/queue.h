#include <pthread.h>
#include <sys/queue.h>

#include "jobs.h"

struct queue {
	struct job_clist jobs;
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
