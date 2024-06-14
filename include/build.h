#include <pthread.h>
#include <sys/queue.h>

#include "queue.h"

struct build_thread {
	pthread_t tid;
	struct queue *queue;
};

void *build_thread_entry(void *queue_thread);
int build_thread_new(struct build_thread **build_thread, struct queue *q);
