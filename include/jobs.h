#include <stdio.h>
#include <sys/queue.h>

#ifndef JOBS_H

LIST_HEAD(output_dlist, output);
struct output {
	char *name, *store_path;
	LIST_ENTRY(output) dlist;
};

LIST_HEAD(job_dlist, job);
CIRCLEQ_HEAD(job_clist, job);
struct job {
	char *name, *drv_path;
	struct output_dlist outputs;
	struct job_dlist deps;

	/* TODO: replace dlist with clist jobs.c */
	LIST_ENTRY(job) dlist;
	CIRCLEQ_ENTRY(job) clist;
};

int jobs_init(FILE **stream);
int job_new(struct job **j, char *name, char *drv_path);
void job_free(struct job *j);
int job_read(FILE *stream, struct job **jobs);

#define JOBS_H
#endif
