e = executable(
	'evanix',
        [
		'evanix.c',
		'jobs.c',
		'util.c',
		'queue.c',
		'build.c',
		'htab.c',
		'solver_util.c',
		'solver_greedy.c',
	],

	include_directories: evanix_inc,
	dependencies: [ cjson_dep, highs_dep ],
	install: true,
)

test('evanix', e)
