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

typedef enum {
	JOB_READ_SUCCESS = 0,
	JOB_READ_EOF = 1,
	JOB_READ_EVAL_ERR = 2,
	JOB_READ_JSON_INVAL = 3,
} job_read_state_t;
int job_read(FILE *stream, struct job **jobs);

int jobs_init(FILE **stream);
int job_new(struct job **j, char *name, char *drv_path);
void job_free(struct job *j);

#define JOBS_H
#endif
