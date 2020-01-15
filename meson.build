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
wayland_protos = dependency('wayland-protocols', version: '>=1.14')

subdir('protocol')

executable(
	'wlr_cpu_pipewire',
	files([
		'screencast.c'
	]),
	dependencies: [
		wayland_client,
    wlr_protos,
    pipewire,
    spa,
    threads,
		rt
	],
	install: true,
)
