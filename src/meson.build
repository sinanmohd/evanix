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
		'solver_sjf.c',
		'nix.c',
		'derivation.c',
		'cache.c',
	],

	dependencies: [
		cjson_dep,
		highs_dep,
		sqlite_dep,
		nix_store_dep,
		libcurl_dep,
	],

	include_directories: evanix_inc,
	install: true,
	c_args: [f'-DNIX_EVAL_JOBS_PATH=@NIX_EVAL_JOBS_PATH@'],
)


foreach solver : ['sjf', 'highs', 'conformity']
	test(f'evanix-@solver@', e, args: [
		'--dry-run',
		'--close-unused-fd=false',
		'--max-builds=3',
		'--solver-report',
		f'--solver=@solver@',
		'--flake',
		'../#packages.x86_64-linux'
	])
endforeach
