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
		'-Wvla',
	],
	language: 'c',
)

cjson_dep = dependency('libcjson')
evanix_inc = include_directories('include')

subdir('src')
