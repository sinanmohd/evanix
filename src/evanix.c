#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "queue.h"
#include "util.h"

int main(void)
{
	struct queue_thread *queue_thread = NULL;
	struct build_thread *build_thread = NULL;
	FILE *stream = NULL;
	int ret = 0;

	ret = jobs_init(&stream);
	if (ret < 0)
		goto out_free;

	ret = queue_thread_new(&queue_thread, stream);
	if (ret < 0) {
		free(stream);
		goto out_free;
	}

	ret = build_thread_new(&build_thread, queue_thread->queue);
	if (ret < 0) {
		free(stream);
		goto out_free;
	}

	ret = pthread_create(&queue_thread->tid, NULL, queue_thread_entry,
			     queue_thread);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	ret = pthread_create(&build_thread->tid, NULL, build_thread_entry,
			     build_thread);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	ret = pthread_join(queue_thread->tid, NULL);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	ret = pthread_join(build_thread->tid, NULL);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

out_free:
	queue_thread_free(queue_thread);
	free(build_thread);
	exit(ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}
