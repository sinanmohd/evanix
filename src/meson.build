e = executable(
	'evanix',
        'evanix.c',

	include_directories: evanix_inc,
	dependencies: cjson_dep,
	install: true,
)

test('evanix', e)
