NIX_EVAL_JOBS_PATH = get_option('NIX_EVAL_JOBS_PATH')
e = executable(
	'evanix',
        [
		'evanix.c',
		'jobs.c',
		'util.c',
		'queue.c',
		'build.c',
		'jobid.c',
		'solver_conformity.c',
		'solver_highs.c',
	],

	include_directories: evanix_inc,
	dependencies: [ cjson_dep, highs_dep ],
	install: true,
	c_args: [f'-DNIX_EVAL_JOBS_PATH=@NIX_EVAL_JOBS_PATH@'],
)

test('evanix', e)
