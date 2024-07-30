dag_test = executable(
	'dag_test',
        [
		'dag.c',
		'../src/jobs.c',
		'../src/util.c',
		'../src/queue.c',
		'../src/build.c',
		'../src/jobid.c',
		'../src/solver_conformity.c',
		'../src/solver_highs.c',
		'../src/solver_sjf.c',
	],

	include_directories: evanix_inc,
	dependencies: [ cjson_dep, highs_dep ],
)

test('dag', dag_test)
