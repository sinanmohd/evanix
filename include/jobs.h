#include <stdio.h>
#include <sys/queue.h>

LIST_HEAD(output_dlist, output);
struct output {
	char *name, *store_path;
	LIST_ENTRY(output) dlist;
};

LIST_HEAD(job_dlist, job);
struct job {
	char *drv_path, *name;
	struct output_dlist outputs;
	struct job_dlist deps;

	LIST_ENTRY(job) dlist;
};

int jobs_init(FILE **stream);
int job_new(struct job **j, char *name, char *drv_path);
void job_free(struct job *j);
int jobs_read(FILE *stream, struct job *jobs);
