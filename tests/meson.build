dag_test = executable(
	'dag_test',
        [
		'dag.c',
		'../src/jobs.c',
		'../src/util.c',
		'../src/queue.c',
	],

	include_directories: evanix_inc,
	dependencies: [ cjson_dep, highs_dep ],
)

test('dag', dag_test)
