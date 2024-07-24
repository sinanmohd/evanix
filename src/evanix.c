#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "evanix.h"
#include "queue.h"
#include "solver_greedy.h"
#include "solver_highs.h"
#include "util.h"

static const char usage[] =
	"Usage: evanix [options] expr\n"
	"\n"
	"  -h, --help                       Show help message and quit.\n"
	"  -f, --flake                      Build a flake.\n"
	"  -d, --dry-run                    Show what derivations would be "
	"built.\n"
	"  -s, --system                     System to build for.\n"
	"  -m, --max-build                  Max number of builds.\n"
	"  -b, --break-evanix               Enable experimental features.\n"
	"  -r, --solver-report              Print solver report.\n"
	"  -p, --pipelined          <bool>  Use evanix build pipeline.\n"
	"  -l, --check_cache-status <bool>  Perform cache locality check.\n"
	"  -c, --close-unused-fd    <bool>  Close stderr on exec.\n"
	"  -k, --solver       highs|greedy  Solver to use.\n"
	"\n";

struct evanix_opts_t evanix_opts = {
	.close_unused_fd = true,
	.isflake = false,
	.ispipelined = true,
	.isdryrun = false,
	.max_build = 0,
	.system = NULL,
	.solver_report = false,
	.check_cache_status = true,
	.solver = solver_greedy,
	.break_evanix = false,
};

static int evanix_build_thread_create(struct build_thread *build_thread);
static int evanix(char *expr);

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

static int evanix(char *expr)
{
	struct queue_thread *queue_thread = NULL;
	struct build_thread *build_thread = NULL;
	FILE *jobsStream = NULL; /* nix-eval-jobs stdout */
	int ret = 0;

	ret = jobs_init(&jobsStream, expr);
	if (ret < 0)
		goto out_free;

	ret = queue_thread_new(&queue_thread, jobsStream);
	if (ret < 0) {
		free(jobsStream);
		goto out_free;
	}

	ret = build_thread_new(&build_thread, queue_thread->queue);
	if (ret < 0) {
		free(jobsStream);
		goto out_free;
	}

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
	queue_thread_free(queue_thread);
	free(build_thread);
	return ret;
}

int opts_read(struct evanix_opts_t *opts, char **expr, int argc, char *argv[])
{
	extern int optind, opterr, optopt;
	extern char *optarg;
	int ret, longindex, c;

	static struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"flake", no_argument, NULL, 'f'},
		{"dry-run", no_argument, NULL, 'd'},
		{"break-evanix", no_argument, NULL, 'b'},
		{"solver", required_argument, NULL, 'k'},
		{"system", required_argument, NULL, 's'},
		{"solver-report", no_argument, NULL, 'r'},
		{"max-build", required_argument, NULL, 'm'},
		{"pipelined", required_argument, NULL, 'p'},
		{"close-unused-fd", required_argument, NULL, 'c'},
		{"check-cache-status", required_argument, NULL, 'l'},
		{NULL, 0, NULL, 0},
	};

	while ((c = getopt_long(argc, argv, "hfds:r::m:p:c:l:k:", longopts,
				&longindex)) != -1) {
		switch (c) {
		case 'h':
			printf("%s", usage);
			return -EINVAL;
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
			opts->system = optarg;
			break;
		case 'r':
			opts->solver_report = true;
			break;
		case 'k':
			if (!strcmp(optarg, "greedy")) {
				opts->solver = solver_greedy;
			} else if (!strcmp(optarg, "highs")) {
				opts->solver = solver_highs;
			} else {
				fprintf(stderr,
					"option -%c has an invalid solver "
					"argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				return -EINVAL;
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
				return -EINVAL;
			}

			opts->max_build = ret;
			break;
		case 'p':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option -%c requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					c);
				return -EINVAL;
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
				return -EINVAL;
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
				return -EINVAL;
			}

			opts->check_cache_status = ret;
			break;
		default:
			fprintf(stderr,
				"Try 'evanix --help' for more information.\n");
			return -EINVAL;
			break;
		}
	}
	if (optind != argc - 1) {
		fprintf(stderr, "evanix: invalid expr operand\n"
				"Try 'evanix --help' for more information.\n");
		return -EINVAL;
	}

	if (opts->solver == solver_highs) {
		if (opts->break_evanix == false) {
			fprintf(stderr, "Running --solver=highs without "
					"--break-evanix\n");
			return -EINVAL;
		}

		opts->ispipelined = false;
	}

	*expr = argv[optind];
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

	exit(EXIT_SUCCESS);
}
