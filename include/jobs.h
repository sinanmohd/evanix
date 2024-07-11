#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>

#ifndef JOBS_H

struct output {
	char *name, *store_path;
};

struct job {
	char *name, *drv_path, *nix_attr_name;
	bool scheduled;
	bool insubstituters;
	size_t outputs_size, outputs_filled;
	struct output **outputs;

	/* DAG */
	size_t deps_size, deps_filled;
	struct job **deps;
	size_t parents_size, parents_filled;
	struct job **parents;

	/* queue */
	CIRCLEQ_ENTRY(job) clist;

	/* solver */
	ssize_t id;
	bool stale;
	bool reported;
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

/* Spawns nix-eval-jobs and connects its stdout to stream */
int jobs_init(FILE **stream, char *expr);
void job_free(struct job *j);
int job_parents_list_insert(struct job *job, struct job *parent);
void job_deps_list_rm(struct job *job, struct job *dep);
void job_stale_set(struct job *job);

#define JOBS_H
#endif
