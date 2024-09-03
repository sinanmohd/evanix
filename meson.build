project(
        'evanix',
        'c',
        version : '0.1',
        license: 'GPL-3.0-only',
        default_options : [
                'c_std=c99',
                'warning_level=2',
        ]
)

add_project_arguments(
	[
                '-D_POSIX_C_SOURCE=200809L',
                '-D_GNU_SOURCE',
		'-Wvla',
	],
	language: 'c',
)

cjson_dep = dependency('libcjson')
nix_store_dep = dependency('nix-store-c')
highs_dep = dependency('highs')
sqlite_dep = dependency('sqlite3')
evanix_inc = include_directories('include')

if get_option('build-python')
        py = import('python').find_installation()
        py.extension_module(
                'evanix',
                'src/evanix-py.c',
                install: true,
        )
endif

subdir('src')
subdir('tests')
