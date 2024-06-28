#include <stdbool.h>
#include <stdio.h>
#include <sys/queue.h>

#ifndef JOBS_H

struct output {
	char *name, *store_path;
};

struct job {
	char *name, *drv_path, *attr;
	bool transitive;
	bool insubstituters;

	size_t outputs_size, outputs_filled;
	struct output **outputs;

	size_t deps_size, deps_filled;
	struct job **deps;

	size_t parents_size, parents_filled;
	struct job **parents;

	CIRCLEQ_ENTRY(job) clist;
};
CIRCLEQ_HEAD(job_clist, job);

typedef enum {
	JOB_READ_SUCCESS = 0,
	JOB_READ_EOF = 1,
	JOB_READ_EVAL_ERR = 2,
	JOB_READ_JSON_INVAL = 3,
	JOB_READ_CACHED = 4,
	JOB_READ_SYS_MISMATCH = 5,
} job_read_state_t;
int job_read(FILE *stream, struct job **jobs);

int jobs_init(FILE **stream, char *expr);
void job_free(struct job *j);
int job_parents_list_insert(struct job *job, struct job *parent);
void job_deps_list_rm(struct job *job, struct job *dep);

#define JOBS_H
#endif
