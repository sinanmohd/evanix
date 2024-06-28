#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "evanix.h"
#include "queue.h"
#include "util.h"

static const char usage[] =
	"Usage: evanix [options] expr\n"
	"\n"
	"  -h, --help                      Show help message and quit.\n"
	"  -f, --flake                     Build a flake.\n"
	"  -d, --dry-run                   Show what derivations would be "
	"built.\n"
	"  -s, --system                    System to build for."
	"  -p, --pipelined         <bool>  Use evanix build pipeline.\n"
	"  -c, --close-stderr-exec <bool>  Close stderr on exec.\n"
	"\n";

struct evanix_opts_t evanix_opts = {
	.close_stderr_exec = true,
	.isflake = false,
	.ispipelined = true,
	.isdryrun = false,
	.system = NULL,
};

static int evanix(char *expr);

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
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	if (evanix_opts.ispipelined)
		ret = pthread_create(&build_thread->tid, NULL,
				     build_thread_entry, build_thread);
	else
		ret = pthread_join(queue_thread->tid, NULL);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	if (evanix_opts.ispipelined)
		ret = pthread_join(queue_thread->tid, NULL);
	else
		ret = pthread_create(&build_thread->tid, NULL,
				     build_thread_entry, build_thread);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

	ret = pthread_join(build_thread->tid, NULL);
	if (ret < 0) {
		print_err("%s", strerror(ret));
		goto out_free;
	}

out_free:
	queue_thread_free(queue_thread);
	free(build_thread);
	return ret;
}

int main(int argc, char *argv[])
{
	extern int optind, opterr, optopt;
	extern char *optarg;
	int ret, longindex;

	static struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"flake", no_argument, NULL, 'f'},
		{"dry-run", no_argument, NULL, 'd'},
		{"system", required_argument, NULL, 's'},
		{"pipelined", required_argument, NULL, 'p'},
		{"close-stderr-exec", required_argument, NULL, 'c'},
		{NULL, 0, NULL, 0},
	};

	while ((ret = getopt_long(argc, argv, "", longopts, &longindex)) !=
	       -1) {
		switch (ret) {
		case 'h':
			printf("%s", usage);
			exit(EXIT_SUCCESS);
			break;
		case 'f':
			evanix_opts.isflake = true;
			break;
		case 'd':
			evanix_opts.isdryrun = true;
			break;
		case 's':
			evanix_opts.system = optarg;
			break;
		case 'p':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option --%s requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					longopts[longindex].name);
				exit(EXIT_FAILURE);
			}

			evanix_opts.ispipelined = ret;
			break;
		case 'c':
			ret = atob(optarg);
			if (ret < 0) {
				fprintf(stderr,
					"option --%s requires a bool argument\n"
					"Try 'evanix --help' for more "
					"information.\n",
					longopts[longindex].name);
				exit(EXIT_FAILURE);
			}

			evanix_opts.close_stderr_exec = ret;
			break;
		default:
			fprintf(stderr,
				"Try 'evanix --help' for more information.\n");
			exit(EXIT_FAILURE);
			break;
		}
	}
	if (optind != argc - 1) {
		fprintf(stderr, "evanix: invalid expr operand\n"
				"Try 'evanix --help' for more information.\n");
		exit(EXIT_FAILURE);
	}

	ret = evanix(argv[optind]);
	if (ret < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
