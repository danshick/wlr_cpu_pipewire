project(
	'wlr_cpu_pipewire',
	'c',
	version: '0.0.0',
	license: 'MIT',
	meson_version: '>=0.46.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true'
	],
)

add_project_arguments('-g', language : 'c')

add_project_arguments('-Wno-unused-parameter', language: 'c')
add_project_arguments('-Wno-unused-function', language: 'c')

cc = meson.get_compiler('c')

threads = dependency('threads')
rt = cc.find_library('rt')

pipewire = dependency('libpipewire-0.2')
spa = dependency('libspa-0.1')

wayland_client = dependency('wayland-client')

inc = include_directories('include')

subdir('protocol')

sources = [
		'src/main.c',
		'src/wlr_screencopy.c',
		'src/pipewire_screencopy.c',
		'src/screencast_common.c'
]

dependencies = [
		wayland_client,
    wlr_protos,
    pipewire,
    spa,
    threads,
		rt
]

executable(
	'wlr_cpu_pipewire',
	files(sources),
	dependencies: dependencies,
	include_directories: [inc],
	install: true,
)
