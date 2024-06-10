#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "jobs.h"
#include "util.h"

static int job_output_insert(struct job *j, char *name, char *store_path);
static void output_free(struct output *output);

int job_read(FILE *stream, struct job **job)
{
	int ret;
	struct job *dep_job;
	cJSON *root, *temp, *input_drvs, *array;
	char *name = NULL;
	char *out_name = NULL;
	char *drv_path = NULL;

	ret = json_streaming_read(stream, &root);
	if (ret < 0)
		return ret;

	temp = cJSON_GetObjectItemCaseSensitive(root, "name");
	if (!cJSON_IsString(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	name = strdup(temp->valuestring);
	if (name == NULL) {
		ret = -EPERM;
		goto out_free;
	}

	temp = cJSON_GetObjectItemCaseSensitive(root, "drvPath");
	if (!cJSON_IsString(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	drv_path = strdup(temp->valuestring);
	if (drv_path == NULL) {
		ret = -EPERM;
		goto out_free;
	}

	ret = job_new(job, name, drv_path);
	if (ret < 0)
		goto out_free;

	input_drvs = cJSON_GetObjectItemCaseSensitive(root, "inputDrvs");
	for (temp = input_drvs; temp != NULL; temp = temp->next) {
		array = cJSON_GetObjectItemCaseSensitive(temp, temp->string);
		if (!cJSON_IsArray(array)) {
			ret = -EPERM;
			job_free(*job);
			goto out_free;
		}

		drv_path = strdup(temp->string);
		if (drv_path == NULL) {
			ret = -EPERM;
			job_free(*job);
			goto out_free;
		}

		ret = job_new(&dep_job, NULL, drv_path);
		if (ret < 0) {
			ret = -EPERM;
			job_free(*job);
			goto out_free;
		}

		for (; array != NULL; array = array->next) {
			out_name = strdup(array->string);
			ret = job_output_insert(dep_job, out_name, NULL);
			if (ret < 0) {
				job_free(*job);
				job_free(dep_job);
				goto out_free;
			}
		}

		drv_path = NULL;
		out_name = NULL;
		LIST_INSERT_HEAD(&(*job)->deps, dep_job, dlist);
	}

out_free:
	cJSON_Delete(root);

	if (ret < 0) {
		print_err("%s", "Invalid JSON");
		free(name);
		free(drv_path);
		free(out_name);
	}
	return ret;
}

static void output_free(struct output *output)
{
	if (output == NULL)
		return;

	free(output->name);
	free(output->store_path);

	free(output);
}

void job_free(struct job *job)
{
	struct job *j;
	struct output *o;

	if (job == NULL)
		return;

	free(job->name);
	free(job->drv_path);

	LIST_FOREACH (o, &job->outputs, dlist)
		output_free(o);

	LIST_FOREACH (j, &job->deps, dlist)
		job_free(j);

	free(job);
}

static int job_output_insert(struct job *j, char *name, char *store_path)
{
	struct output *o;

	o = malloc(sizeof(*o));
	if (o == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	o->name = name;
	o->store_path = store_path;
	LIST_INSERT_HEAD(&j->outputs, o, dlist);

	return 0;
}

int job_new(struct job **j, char *name, char *drv_path)
{
	struct job *job;

	job = malloc(sizeof(*job));
	if (job == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	job->name = name;
	job->drv_path = drv_path;
	LIST_INIT(&job->deps);
	LIST_INIT(&job->outputs);

	*j = job;
	return 0;
}

int jobs_init(FILE **stream)
{
	int ret;

	/* TODO: proproperly handle args */
	char *const args[] = {
		"--flake",
		"github:sinanmohd/evanix#packages.x86_64-linux.evanix",
	};

	/* the package is wrapProgram-ed with nix-eval-jobs  */
	ret = vpopen(stream, "nix-eval-jobs", args);
	return ret;
}
