#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "jobs.h"
#include "util.h"

static void output_free(struct output *output);
static int job_output_insert(struct job *j, char *name, char *store_path);
static int job_read_inputdrvs(struct job *job, cJSON *input_drvs);
static int job_read_outputs(struct job *job, cJSON *outputs);

static void output_free(struct output *output)
{
	if (output == NULL)
		return;

	free(output->name);
	free(output->store_path);

	free(output);
}

static int job_output_insert(struct job *j, char *name, char *store_path)
{
	struct output *o;
	int ret = 0;

	o = malloc(sizeof(*o));
	if (o == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	o->name = strdup(name);
	if (o->name == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_o;
	}
	if (store_path != NULL) {
		o->store_path = strdup(store_path);
		if (o->store_path == NULL) {
			print_err("%s", strerror(errno));
			ret = -errno;
			goto out_free_name;
		}
	} else {
		o->store_path = NULL;
	}

out_free_name:
	if (ret < 0)
		free(o->name);
out_free_o:
	if (ret < 0)
		free(o);
	else
		LIST_INSERT_HEAD(&j->outputs, o, dlist);

	return 0;
}

static int job_read_inputdrvs(struct job *job, cJSON *input_drvs)
{
	cJSON *output;

	struct job *dep_job = NULL;
	int ret = 0;

	for (cJSON *array = input_drvs; array != NULL; array = array->next) {
		ret = job_new(&dep_job, NULL, array->string);
		if (ret < 0)
			goto out_free_dep_job;

		cJSON_ArrayForEach (output, array) {
			ret = job_output_insert(dep_job, output->valuestring, NULL);
			if (ret < 0) {
				job_free(dep_job);
				goto out_free_dep_job;
			}
		}

		LIST_INSERT_HEAD(&job->deps, dep_job, dlist);
		dep_job = NULL;
	}

out_free_dep_job:
	if (ret < 0)
		job_free(dep_job);

	return ret;
}

static int job_read_outputs(struct job *job, cJSON *outputs)
{
	int ret;

	for (cJSON *i = outputs; i != NULL; i = i->next) {
		ret = job_output_insert(job, i->string, i->valuestring);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int job_read(FILE *stream, struct job **job)
{
	cJSON *temp;

	char *drv_path = NULL;
	struct job *j = NULL;
	cJSON *root = NULL;
	char *name = NULL;
	int ret = 0;

	ret = json_streaming_read(stream, &root);
	if (ret < 0 || ret == -EOF)
		return JOB_READ_EOF;

	temp = cJSON_GetObjectItemCaseSensitive(root, "error");
	if (cJSON_IsString(temp)) {
		puts(temp->valuestring);
		ret = JOB_READ_EVAL_ERR;
		goto out_free;
	}

	temp = cJSON_GetObjectItemCaseSensitive(root, "name");
	if (!cJSON_IsString(temp)) {
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	name = temp->valuestring;

	temp = cJSON_GetObjectItemCaseSensitive(root, "drvPath");
	if (!cJSON_IsString(temp)) {
		free(name);
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	drv_path = temp->valuestring;

	ret = job_new(&j, name, drv_path);
	if (ret < 0)
		goto out_free;

	temp = cJSON_GetObjectItemCaseSensitive(root, "inputDrvs");
	if (!cJSON_IsObject(temp)) {
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	ret = job_read_inputdrvs(j, temp->child);
	if (ret < 0)
		goto out_free;

	temp = cJSON_GetObjectItemCaseSensitive(root, "outputs");
	if (!cJSON_IsObject(temp)) {
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	ret = job_read_outputs(j, temp->child);
	if (ret < 0)
		goto out_free;

out_free:
	cJSON_Delete(root);
	if (ret != JOB_READ_SUCCESS)
		job_free(j);
	else
		*job = j;
	if (ret == JOB_READ_JSON_INVAL)
		print_err("%s", "Invalid JSON");

	return ret;
}

void job_free(struct job *job)
{
	struct job *job_cur, *job_next;
	struct output *op_cur, *op_next;

	if (job == NULL)
		return;

	free(job->name);
	free(job->drv_path);

	LIST_FOREACH_FREE(op_cur, op_next, &job->outputs, dlist, output_free);
	LIST_FOREACH_FREE(job_cur, job_next, &job->deps, dlist, job_free);

	free(job);
}

int job_new(struct job **j, char *name, char *drv_path)
{
	struct job *job;
	int ret = 0;

	job = malloc(sizeof(*job));
	if (job == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	if (name != NULL) {
		job->name = strdup(name);
		if (job->name == NULL) {
			print_err("%s", strerror(errno));
			ret = -errno;
			goto out_free_job;
		}
	} else {
		job->name = NULL;
	}

	job->drv_path = strdup(drv_path);
	if (job->drv_path == NULL) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_name;
	}

	LIST_INIT(&job->deps);
	LIST_INIT(&job->outputs);

out_free_name:
	if (ret < 0)
		free(job->name);
out_free_job:
	if (ret < 0)
		free(job);
	else
		*j = job;

	return ret;
}

int jobs_init(FILE **stream)
{
	int ret;

	/* TODO: proproperly handle args */
	char *const args[] = {
		"nix-eval-jobs",
		"--flake",
		"github:sinanmohd/evanix#packages.x86_64-linux",
		NULL,
	};

	/* the package is wrapProgram-ed with nix-eval-jobs  */
	ret = vpopen(stream, "nix-eval-jobs", args);
	return ret;
}
