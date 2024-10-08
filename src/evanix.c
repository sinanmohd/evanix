#include <errno.h>
#include <getopt.h>
#include <nix/nix_api_value.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "evanix.h"
#include "nix.h"
#include "queue.h"
#include "solver_conformity.h"
#include "solver_highs.h"
#include "solver_sjf.h"
#include "util.h"

static const char usage[] =
	"Usage: evanix [options] expr\n"
	"\n"
	"  -h, --help                         Show help message and quit.\n"
	"  -f, --flake                        Build a flake.\n"
	"  -d, --dry-run                      Show what derivations would be "
	"built.\n"
	"  -s, --system                       System to build for.\n"
	"  -m, --max-builds                   Max number of builds.\n"
	"  -t, --max-time                     Max time available in seconds.\n"
	"  -b, --break-evanix                 Enable experimental features.\n"
	"  -r, --solver-report                Print solver report.\n"
	"  -p, --pipelined            <bool>  Use evanix build pipeline.\n"
	"  -l, --check_cache-status   <bool>  Perform cache locality check.\n"
	"  -c, --close-unused-fd      <bool>  Close stderr on exec.\n"
	"  -e, --statistics           <path>  Path to time statistics "
	"database.\n"
	"  -k, --solver sjf|conformity|highs  Solver to use.\n"
	"\n";

struct evanix_opts_t evanix_opts = {
	.close_unused_fd = true,
	.isflake = false,
	.ispipelined = true,
	.isdryrun = false,
	.max_builds = 0,
	.max_time = 0,
	.system = NULL,
	.solver_report = false,
	.check_cache_status = true,
	.solver = solver_highs,
	.break_evanix = false,
	.statistics.db = NULL,
	.statistics.statement = NULL,
};

static int evanix_build_thread_create(struct build_thread *build_thread);
static int evanix(char *expr);
static int evanix_free(struct evanix_opts_t *opts);
static int opts_read(struct evanix_opts_t *opts, char **expr, int argc,
		     char *argv[]);
static int evanix_opts_system_set(struct evanix_opts_t *opts,
				  nix_c_context *nix_ctx);

/* This function returns errno on failure, consistent with the POSIX threads
 * functions, rather than returning -errno. */
static int evanix_build_thread_create(struct build_thread *build_thread)
{
	int ret;

	ret = pthread_create(&build_thread->tid, NULL, build_thread_entry,
			     build_thread);
	if (ret != 0)
		return ret;

	ret = pthread_setname_np(build_thread->tid, "evanix_build");
	if (ret != 0)
		return ret;

	return 0;
}

static int evanix_opts_system_set(struct evanix_opts_t *opts,
				  nix_c_context *nix_ctx)
{
	nix_err nix_ret;

	if (opts->system)
		return 0;

	nix_ret = nix_setting_get(nix_ctx, "system", _nix_get_string_strdup,
				  &opts->system);
	if (nix_ret != NIX_OK) {
		print_err("%s", nix_err_msg(NULL, nix_ctx, NULL));
		return -EPERM;
	} else if (opts->system == NULL) {
		return -EPERM;
	}

	return 0;
}

static int evanix(char *expr)
{
	nix_c_context *nix_ctx = NULL;
	struct queue_thread *queue_thread = NULL;
	struct build_thread *build_thread = NULL;
	FILE *jobs_stream = NULL; /* nix-eval-jobs stdout */
	int ret = 0;

	ret = _nix_init(&nix_ctx);
	if (ret < 0)
		goto out_free;

	ret = evanix_opts_system_set(&evanix_opts, nix_ctx);
	if (ret < 0)
		goto out_free;

	ret = jobs_init(&jobs_stream, expr);
	if (ret < 0)
		goto out_free;

	ret = queue_thread_new(&queue_thread, jobs_stream);
	if (ret < 0)
		goto out_free;

	ret = build_thread_new(&build_thread, queue_thread->queue);
	if (ret < 0)
		goto out_free;

	ret = pthread_create(&queue_thread->tid, NULL, queue_thread_entry,
			     queue_thread);
	if (ret != 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}
	ret = pthread_setname_np(queue_thread->tid, "evanix_queue");
	if (ret != 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	if (evanix_opts.ispipelined)
		ret = evanix_build_thread_create(build_thread);
	else
		ret = pthread_join(queue_thread->tid, NULL);
	if (ret != 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	if (evanix_opts.ispipelined)
		ret = pthread_join(queue_thread->tid, NULL);
	else
		ret = evanix_build_thread_create(build_thread);
	if (ret != 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	ret = pthread_join(build_thread->tid, NULL);
	if (ret != 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

out_free:
	nix_c_context_free(nix_ctx);
	fclose(jobs_stream);
	queue_thread_free(queue_thread);
	free(build_thread);

	return ret;
}

static int opts_read(struct evanix_opts_t *opts, char **expr, int argc,
		     char *argv[])
{
	extern int optind, opterr, optopt;
	extern char *optarg;
	int longindex, c;

	const char *query = "SELECT statistics.mean_duration "
			    "FROM statistics "
			    "WHERE statistics.pname = ? "
			    "LIMIT 1 ";

	int ret = 0;

	static struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"flake", no_argument, NULL, 'f'},
		{"dry-run", no_argument, NULL, 'd'},
		{"break-evanix", no_argument, NULL, 'b'},
		{"solver", required_argument, NULL, 'k'},
		{"system", required_argument, NULL, 's'},
		{"solver-report", no_argument, NULL, 'r'},
		{"max-time", required_argument, NULL, 't'},
		{"statistics", required_argument, NULL, 'a'},
		{"pipelined", required_argument, NULL, 'p'},
		{"max-builds", required_argument, NULL, 'm'},
		{"close-unused-fd", required_argument, NULL, 'c'},
		{"check-cache-status", required_argument, NULL, 'l'},
		{NULL, 0, NULL, 0},
	};

	while ((c = getopt_long(argc, argv, "hfds:r::m:p:c:l:k:a:t:", longopts,
				&longindex)) != -1) {
		switch (c) {
		case 'h':
			printf("%s", usage);
			return 0;
			break;
		case 'f':
			opts->isflake = true;
			break;
		case 'b':
			opts->break_evanix = true;
			break;
		case 'd':
			opts->isdryrun = true;
			break;
		case 's':
			opts->system = strdup(optarg);
			if (opts->system == NULL) {
				print_err("%s", strerror(errno));
				ret = -errno;
				goto out_free_evanix;
			}

			break;
		case 'r':
			opts->solver_report = true;
			break;
		case 'a':
			if (opts->statistics.db) {
				fprintf(stderr,
					"option -%c can't be redefined "
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			ret = sqlite3_open_v2(optarg, &opts->statistics.db,
					      SQLITE_OPEN_READONLY |
						      SQLITE_OPEN_FULLMUTEX,
					      NULL);
			if (ret != SQLITE_OK) {
				print_err("Can't open database: %s",
					  sqlite3_errmsg(opts->statistics.db));
				ret = -EPERM;
				goto out_free_evanix;
			}
			ret = sqlite3_prepare_v2(opts->statistics.db, query, -1,
						 &opts->statistics.statement,
						 NULL);
			if (ret != SQLITE_OK) {
				print_err("%s", "Failed to prepare sql");
				ret = -EPERM;
				goto out_free_evanix;
			}

			break;
		case 'k':
			if (!strcmp(optarg, "conformity")) {
				opts->solver = solver_conformity;
			} else if (!strcmp(optarg, "highs")) {
				opts->solver = solver_highs;
			} else if (!strcmp(optarg, "sjf")) {
				opts->solver = solver_sjf;
			} else {
				fprintf(stderr,
					"option -%c has an invalid solver "
					"argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}
			break;
		case 'm':
			ret = atoi(optarg);
			if (ret <= 0) {
				fprintf(stderr,
					"option -%c requires a natural number "
					"argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			opts->max_builds = ret;
			break;
		case 't':
			ret = atoi(optarg);
			if (ret <= 0) {
				fprintf(stderr,
					"option -%c requires a natural number "
					"argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			opts->max_time = ret;
			break;
		case 'p':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option -%c requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			opts->ispipelined = ret;
			break;
		case 'c':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option -%c requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			opts->close_unused_fd = ret;
			break;
		case 'l':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option -%c requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				ret = -EINVAL;
				goto out_free_evanix;
			}

			opts->check_cache_status = ret;
			break;
		default:
			fprintf(stderr,
				"Try 'evanix --help' for more information.\n");
			ret = -EINVAL;
			goto out_free_evanix;
			break;
		}
	}
	if (optind != argc - 1) {
		fprintf(stderr, "evanix: invalid expr operand\n"
				"Try 'evanix --help' for more information.\n");
		ret = -EINVAL;
		goto out_free_evanix;
	} else if (opts->max_time && opts->max_builds) {
		fprintf(stderr, "evanix: options --max-time and --max-builds "
				"are mutually exclusive\n"
				"Try 'evanix --help' for more information.\n");
		ret = -EINVAL;
		goto out_free_evanix;
	} else if (opts->max_time && !opts->statistics.db) {
		fprintf(stderr,
			"evanix: option --max-time implies --statistics\n"
			"Try 'evanix --help' for more information.\n");
		ret = -EINVAL;
		goto out_free_evanix;
	}

	if (opts->solver == solver_highs &&
	    (opts->max_time || opts->max_builds)) {
		opts->ispipelined = false;
	}

out_free_evanix:
	if (ret < 0)
		evanix_free(opts);
	else
		*expr = argv[optind];

	return ret < 0 ? ret : 0;
}

static int evanix_free(struct evanix_opts_t *opts)
{
	int ret;

	free(opts->system);

	if (opts->statistics.statement) {
		sqlite3_finalize(opts->statistics.statement);
		opts->statistics.statement = NULL;
	}
	if (opts->statistics.db) {
		ret = sqlite3_close(opts->statistics.db);
		if (ret != SQLITE_OK) {
			print_err("Can't open database: %s",
				  sqlite3_errmsg(opts->statistics.db));
			return -EPERM;
		}

		opts->statistics.db = NULL;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char *expr;
	int ret;

	ret = opts_read(&evanix_opts, &expr, argc, argv);
	if (ret < 0)
		exit(EXIT_FAILURE);

	ret = evanix(argv[optind]);
	if (ret < 0)
		exit(EXIT_FAILURE);

	ret = evanix_free(&evanix_opts);
	if (ret < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
