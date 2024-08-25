#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cjson/cJSON.h>
#include <sqlite3.h>

#include "evanix.h"
#include "jobs.h"
#include "util.h"

#ifndef NIX_EVAL_JOBS_PATH
#warning "NIX_EVAL_JOBS_PATH not defined, evanix will rely on PATH instead"
#define NIX_EVAL_JOBS_PATH nix - eval - jobs
#endif

#define XSTR(x) STR(x)
#define STR(x)	#x
#pragma message "NIX_EVAL_JOBS_PATH=" XSTR(NIX_EVAL_JOBS_PATH)

static void output_free(struct output *output);
static int job_new(struct job **j, char *name, char *drv_path, char *attr,
		   struct job *parent);
static int job_output_insert(struct job *j, char *name, char *store_path);
static int job_read_inputdrvs(struct job *job, cJSON *input_drvs);
static int job_read_outputs(struct job *job, cJSON *outputs);
static int job_deps_list_insert(struct job *job, struct job *dep);
static int job_output_list_insert(struct job *job, struct output *output);
static char *drv_path_to_pname(char *drv_path);

static void output_free(struct output *output)
{
	if (output == NULL)
		return;

	free(output->name);
	free(output->store_path);

	free(output);
}

static char *drv_path_to_pname(char *drv_path)
{
	char *pname, *p;

	p = strrchr(drv_path, '/');
	if (p == NULL)
		return NULL;

	p = strchr(p, '-');
	if (p == NULL)
		return NULL;
	p++;
	if (*p == '\0')
		return NULL;

	pname = strdup(p);
	if (pname == NULL)
		return NULL;

	p = strchr(pname, '-');
	if (p != NULL)
		*p = '\0';

	p = strstr(pname, ".drv");
	if (p != NULL)
		*p = '\0';

	return pname;
}

static int job_output_list_insert(struct job *job, struct output *output)
{
	size_t newsize;
	void *ret;

	if (job->outputs_filled < job->outputs_size) {
		job->outputs[job->outputs_filled++] = output;
		return 0;
	}

	newsize = job->outputs_size == 0 ? 1 : job->outputs_size * 2;
	ret = realloc(job->outputs, newsize * sizeof(*job->outputs));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	job->outputs = ret;
	job->outputs_size = newsize;
	job->outputs[job->outputs_filled++] = output;

	return 0;
}

void job_deps_list_rm(struct job *job, struct job *dep)
{
	for (size_t i = 0; i < job->deps_filled; i++) {
		if (job->deps[i] != dep)
			continue;

		job->deps[i] = job->deps[job->deps_filled - 1];
		job->deps_filled -= 1;
		return;
	}
}

static int job_deps_list_insert(struct job *job, struct job *dep)
{
	size_t newsize;
	void *ret;

	if (job->deps_filled < job->deps_size) {
		job->deps[job->deps_filled++] = dep;
		return 0;
	}

	newsize = job->deps_size == 0 ? 1 : job->deps_size * 2;
	ret = realloc(job->deps, newsize * sizeof(*job->deps));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	job->deps = ret;
	job->deps_size = newsize;
	job->deps[job->deps_filled++] = dep;

	return 0;
}

int job_parents_list_insert(struct job *job, struct job *parent)
{
	size_t newsize;
	void *ret;

	if (job->parents_filled < job->parents_size) {
		job->parents[job->parents_filled++] = parent;
		return 0;
	}

	newsize = job->parents_size == 0 ? 1 : job->parents_size * 2;
	ret = realloc(job->parents, newsize * sizeof(*job->parents));
	if (ret == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	job->parents = ret;
	job->parents_size = newsize;
	job->parents[job->parents_filled++] = parent;

	return 0;
}

int job_cost(struct job *job)
{
	int ret;
	char *pname;

	if (!evanix_opts.max_time)
		return 1;

	pname = drv_path_to_pname(job->drv_path);
	if (pname == NULL) {
		print_err("Unable to obtain pname from drv_path: %s",
			  job->drv_path);
		return -EINVAL;
	}

	ret = sqlite3_reset(evanix_opts.estimate.statement);
	if (ret != SQLITE_OK) {
		print_err("%s", "Failed to reset sql statement");
		ret = -EPERM;
		goto out_free_pname;
	}
	ret = sqlite3_bind_text(evanix_opts.estimate.statement, 1, pname, -1,
				NULL);
	if (ret != SQLITE_OK) {
		print_err("%s", "Failed to bind sql");
		ret = -EPERM;
		goto out_free_pname;
	}

	ret = sqlite3_step(evanix_opts.estimate.statement);
	if (ret == SQLITE_DONE) {
		print_err("Failed to acquire statistics for %s", pname);
		ret = -ENOENT;
		goto out_free_pname;
	} else if (ret != SQLITE_ROW) {
		print_err("%s", "Failed to step sql");
		ret = -EPERM;
		goto out_free_pname;
	}

	ret = sqlite3_column_int(evanix_opts.estimate.statement, 0);

out_free_pname:
	free(pname);

	return ret;
}

int job_cost_recursive(struct job *job)
{
	int ret, builds;

	ret = job_cost(job);
	if (ret < 0)
		return ret;
	builds = ret;

	for (size_t i = 0; i < job->deps_filled; i++) {
		if (job->deps[i]->insubstituters)
			continue;

		ret = job_cost(job->deps[i]);
		if (ret < 0)
			return ret;

		builds += ret;
	}

	return builds;
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

	ret = job_output_list_insert(j, o);
	if (ret < 0)
		goto out_free_store_path;

out_free_store_path:
	if (ret < 0)
		free(o->store_path);
out_free_name:
	if (ret < 0)
		free(o->name);
out_free_o:
	if (ret < 0)
		free(o);

	return 0;
}

static int job_read_inputdrvs(struct job *job, cJSON *input_drvs)
{
	cJSON *output;

	struct job *dep_job = NULL;
	int ret = 0;

	for (cJSON *array = input_drvs; array != NULL; array = array->next) {
		ret = job_new(&dep_job, NULL, array->string, NULL, job);
		if (ret < 0)
			goto out_free_dep_job;

		cJSON_ArrayForEach (output, array) {
			ret = job_output_insert(dep_job, output->valuestring,
						NULL);
			if (ret < 0)
				job_free(dep_job);
		}

		ret = job_deps_list_insert(job, dep_job);
		if (ret < 0)
			job_free(dep_job);

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

/* for nix-eval-jobs output only, for dag use the htab instead */
struct job *job_search(struct job *job, const char *drv)
{
	if (!strcmp(drv, job->drv_path))
		return job;

	for (size_t i = 0; i < job->deps_filled; i++) {
		if (!strcmp(drv, job->deps[i]->drv_path))
			return job->deps[i];
	}

	return NULL;
}

static int job_read_cache(struct job *job)
{
	size_t argindex, n;
	FILE *nix_build_stream;
	char *args[4];
	int ret, nlines;
	struct job *j;

	char *line = NULL;
	argindex = 0;
	args[argindex++] = "nix-build";
	args[argindex++] = "--dry-run";
	args[argindex++] = job->drv_path;
	args[argindex++] = NULL;

	ret = vpopen(&nix_build_stream, "nix-build", args, VPOPEN_STDERR);
	if (ret < 0)
		return ret;

	errno = 0;
	nlines = 0;
	for (bool in_fetched_block = false;
	     getline(&line, &n, nix_build_stream) >= 0; nlines++) {
		if (strstr(line, "will be built")) {
			continue;
		} else if (strstr(line, "will be fetched")) {
			in_fetched_block = true;
			continue;
		}

		j = job_search(job, trim(line));
		if (j == NULL) {
			/* nix-eval-jobs doesn't count src */
			continue;
		}

		if (in_fetched_block)
			j->insubstituters = true;
		else
			j->insubstituters = false;

		j->stale = false;
	}
	if (errno != 0) {
		print_err("%s", strerror(errno));
		ret = -errno;
		goto out_free_line;
	} else if (errno == 0 && nlines == 0) {
		ret = JOB_READ_CACHED;
		goto out_free_line;
	}

	/* remove stale deps */
	for (size_t i = 0; i < job->deps_filled;) {
		if (job->deps[i]->stale)
			job_free(job->deps[i]);
		else
			i++;
	}

	ret = 0;

out_free_line:
	free(line);
	fclose(nix_build_stream);

	return ret;
}

int job_read(FILE *stream, struct job **job)
{
	cJSON *temp;

	char *drv_path = NULL;
	struct job *j = NULL;
	cJSON *root = NULL;
	char *attr = NULL;
	char *name = NULL;
	int ret = 0;

	ret = json_streaming_read(stream, &root);
	if (ret < 0 || ret == -EOF)
		return JOB_READ_EOF;

	temp = cJSON_GetObjectItemCaseSensitive(root, "error");
	if (cJSON_IsString(temp)) {
		if (evanix_opts.close_unused_fd)
			puts(temp->valuestring);
		ret = JOB_READ_EVAL_ERR;
		goto out_free;
	}

	if (evanix_opts.system != NULL) {
		temp = cJSON_GetObjectItemCaseSensitive(root, "system");
		if (!cJSON_IsString(temp)) {
			ret = JOB_READ_JSON_INVAL;
			goto out_free;
		}
		if (strcmp(evanix_opts.system, temp->valuestring)) {
			ret = JOB_READ_SYS_MISMATCH;
			goto out_free;
		}
	}

	temp = cJSON_GetObjectItemCaseSensitive(root, "name");
	if (!cJSON_IsString(temp)) {
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	name = temp->valuestring;

	temp = cJSON_GetObjectItemCaseSensitive(root, "attr");
	if (!cJSON_IsString(temp)) {
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	if (temp->valuestring[0] != '\0')
		attr = temp->valuestring;

	temp = cJSON_GetObjectItemCaseSensitive(root, "drvPath");
	if (!cJSON_IsString(temp)) {
		free(name);
		ret = JOB_READ_JSON_INVAL;
		goto out_free;
	}
	drv_path = temp->valuestring;

	ret = job_new(&j, name, drv_path, attr, NULL);
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

	if (evanix_opts.check_cache_status) {
		ret = job_read_cache(j);
		if (ret < 0)
			goto out_free;
	}

	ret = JOB_READ_SUCCESS;

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
	if (job == NULL)
		return;

	/* deps_filled will be decremented by recusrive call to job_free()
	 * itself, see job_deps_list_rm() in the next for loop */
	while (job->deps_filled)
		job_free(*job->deps);
	free(job->deps);

	for (size_t i = 0; i < job->parents_filled; i++)
		job_deps_list_rm(job->parents[i], job);
	free(job->parents);

	for (size_t i = 0; i < job->outputs_filled; i++)
		output_free(job->outputs[i]);
	free(job->outputs);

	free(job->drv_path);
	free(job->name);
	free(job->nix_attr_name);
	free(job);
}

static int job_new(struct job **j, char *name, char *drv_path, char *attr,
		   struct job *parent)
{
	struct job *job;
	int ret = 0;

	job = malloc(sizeof(*job));
	if (job == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}
	job->requested = false;
	job->id = -1;

	job->outputs_size = 0;
	job->outputs_filled = 0;
	job->outputs = NULL;

	job->deps_size = 0;
	job->deps_filled = 0;
	job->deps = NULL;

	job->parents_size = 0;
	job->parents_filled = 0;
	job->parents = NULL;

	if (evanix_opts.check_cache_status) {
		job->stale = true;
	} else {
		job->insubstituters = false;
		job->stale = false;
	}

	if (attr != NULL) {
		job->nix_attr_name = strdup(attr);
		if (job->nix_attr_name == NULL) {
			print_err("%s", strerror(errno));
			ret = -errno;
			goto out_free_job;
		}
	} else {
		job->nix_attr_name = NULL;
	}

	if (name != NULL) {
		job->name = strdup(name);
		if (job->name == NULL) {
			print_err("%s", strerror(errno));
			ret = -errno;
			goto out_free_attr;
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

	if (parent != NULL) {
		ret = job_parents_list_insert(job, parent);
		if (ret < 0)
			goto out_free_drv_path;
	}

out_free_drv_path:
	if (ret < 0)
		free(job->drv_path);
out_free_name:
	if (ret < 0)
		free(job->name);
out_free_attr:
	if (ret < 0)
		free(job->nix_attr_name);
out_free_job:
	if (ret < 0)
		free(job);
	else
		*j = job;

	return ret;
}

int jobs_init(FILE **stream, char *expr)
{
	size_t argindex;
	char *args[4];

	argindex = 0;
	args[argindex++] = XSTR(NIX_EVAL_JOBS_PATH);
	if (evanix_opts.isflake)
		args[argindex++] = "--flake";
	args[argindex++] = expr;
	args[argindex++] = NULL;

	/* the package is wrapProgram-ed with nix-eval-jobs  */
	return vpopen(stream, XSTR(NIX_EVAL_JOBS_PATH), args, VPOPEN_STDOUT);
}

void job_stale_set(struct job *job)
{
	if (job->stale)
		return;

	job->stale = true;
	for (size_t i = 0; i < job->parents_filled; i++)
		job_stale_set(job->parents[i]);
}
