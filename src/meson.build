e = executable(
	'evanix',
        [
		'evanix.c',
		'jobs.c',
		'util.c',
		'queue.c',
		'build.c',
	],

	include_directories: evanix_inc,
	dependencies: cjson_dep,
	install: true,
)

test('evanix', e)
