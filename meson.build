project('io.github.trbjo.bob.launcher', 'vala', 'c',
    version: '0.5.0',
    default_options: [
        'buildtype=release',
        'optimization=3',
        'b_lto=true',
        'prefix=/usr/local'
    ]
)

# use this for debug builds
# meson setup build --buildtype=debug -Db_sanitize=address,undefined -Dc_args='-g -fno-omit-frame-pointer -O0 -falign-functions=16 -falign-loops=16'
# meson setup build --buildtype=debug -Db_sanitize=thread            -Dc_args='-g -fno-omit-frame-pointer -O1'

simple_keyboard = subproject('simple-keyboard')
simple_keyboard_dep = simple_keyboard.get_variable('simple_keyboard_dep')
simple_keyboard_vapi_dir = simple_keyboard.get_variable('simple_keyboard_dep').get_variable('vapi_dir')

thread_manager = subproject('thread-manager')
thread_manager_dep = thread_manager.get_variable('thread_manager_dep')
thread_manager_vapi_dir = thread_manager.get_variable('thread_manager_dep').get_variable('vapi_dir')

file_monitor = subproject('file-monitor')
file_monitor_dep = file_monitor.get_variable('file_monitor_dep')
file_monitor_vapi_dir = file_monitor.get_variable('file_monitor_dep').get_variable('vapi_dir')

plugins_subproj = subproject('plugins')

lib_sources = files(
    'src/api/interfaces.vala',
    'src/core/appsettings.vala',
    'src/core/match/paintable.vala',
    'src/core/match/match.vala',
    'src/core/match/unknown-match.vala',
    'src/core/plugin/plugin-base.vala',
    'src/core/plugin/search-base.vala',
    'src/core/utils.vala',
    'src/gui/utils.vala',
)

c_sources = files(
    'src/C/constants.h',
    'src/C/xxhash-override.h',
    'src/C/access-appinfo-props.c',
    'src/C/access-appinfo-props.h',
    # 'src/C/plugin-base.c',
    # 'src/C/plugin-base.h',
    # 'src/C/search-base.c',
    # 'src/C/search-base.h',
    'src/C/systemd-scope.c',
    'src/C/dbus-scope-monitor.c',
    'src/C/drag-and-drop-handler.c',
    'src/C/data-sink-actions.c',
    'src/C/data-sink-sources.c',
    'src/C/data-sink-plugins.c',
    'src/C/systemd-launcher.c',
    'src/C/common-launcher.c',
    'src/C/launch-context.c',
    'src/C/systemd-launcher.h',
    'src/C/common-launcher.h',
    'src/C/launch-context.h',
    'src/C/file-match.c',
    'src/C/file-match.h',
    'src/C/state.c',
    'src/C/app.c',
    'src/C/utf8-utils.c',
    'src/C/controller.c',
    'src/C/keybindings.c',
    'src/C/icon-cache-service.c',
    'src/C/resize-handle.c',
    'src/C/resize-handle.h',
    'src/C/width-resize-handle.c',
    'src/C/width-resize-handle.h',
    'src/C/launcher-window.c',
    'src/C/launcher-window.h',
    'src/C/plugin-loader.c',
    'src/C/plugin-loader.h',
    'src/C/scroll-controller.c',
    'src/C/scroll-controller.h',
    'src/C/drag-controller.c',
    'src/C/drag-controller.h',
    'src/C/query-container.c',
    'src/C/query-container.h',
    'src/C/css.c',
    'src/C/css.h',
    'src/C/query-container.c',
    'src/C/query-container.h',
    'src/C/match-row-label.c',
    'src/C/match-row-label.h',
    'src/C/description.c',
    'src/C/description.h',
    'src/C/result-box.c',
    'src/C/result-box.h',
    'src/C/main-container.c',
    'src/C/main-container.h',
    'src/C/row-number.c',
    'src/C/row-number.h',
    'src/C/text-image.c',
    'src/C/text-image.h',
    'src/C/match-row.c',
    'src/C/match-row.h',
    'src/C/result-container.c',
    'src/C/highlight.c',
    'src/C/hashset.c',
    'src/C/events.c',
    'src/C/fzy/match.c',
    'src/C/string-utils.c',
    'src/C/gthread_stub.c',
    'src/C/fzy/config.h',
    'src/C/systemd_service_utils.h',
    'src/C/systemd_service_utils.c',
)

gnome = import('gnome')

gresources = gnome.compile_resources(
    'gresources',
    join_paths('data', meson.project_name() + '.gresource.xml'),
    source_dir: 'data',
)

inc_dirs = include_directories(
    'src/C/fzy',
    'src/C',
)

base_deps = [
    dependency('gtk4-layer-shell-0', version: '>= 0.6.0'),
    dependency('gio-unix-2.0'),
    dependency('glib-2.0', version: '>= 2.66.0'),
    dependency('gmodule-2.0'),
    dependency('gtk4', version: '>= 4.0.0'),
    dependency('gtk4-wayland', version: '>= 4.0.0'),
    dependency('wayland-client'),
    meson.get_compiler('vala').find_library('posix'),
    simple_keyboard_dep,
    thread_manager_dep,
    file_monitor_dep,
    meson.get_compiler('c').find_library('m'),
    meson.get_compiler('c').find_library('dbus-1'),
    meson.get_compiler('c').find_library('xxhash'),
]

main_c_args = [
    '-include', join_paths(meson.current_source_dir(), 'src/C/no-thread-init.h'),
    '-I/usr/include/xxhash',
    '-include', join_paths(meson.current_source_dir(), 'src/C/xxhash-override.h'),
]

dbus_c_args = [
'-I/usr/include/dbus-1.0',
'-I/usr/lib/dbus-1.0/include'
]

base_vala_args = [
    '--vapidir=' + join_paths(meson.current_source_dir(), 'vapi'),
    '--pkg', 'launch-context',
    # '--pkg', 'bob-launcher-plugin',
    '--pkg', 'file-match',
    '--pkg', 'icon-cache-service',
    '--pkg', 'controller',
    '--pkg', 'description',
    '--pkg', 'result-container',
    '--pkg', 'data-sink-actions',
    '--pkg', 'levensteihn',
    '--pkg', 'string-utils',
    '--pkg', 'highlight',
    '--pkg', 'hashset',
    '--vapidir=' + thread_manager_vapi_dir,
    '--pkg', 'thread-manager',
]

launcher_lib = shared_library('bob-launcher',
    lib_sources + c_sources + gresources,
    dependencies: base_deps,
    include_directories: inc_dirs,
    c_args: main_c_args + dbus_c_args,
    vala_args: base_vala_args,
    install: true,
    install_dir: get_option('libdir')
)

systemd_sources = files(
    'src/C/systemd_service_impl.c',
)

libsystemd_service_impl = shared_library('systemd-service-impl',
    systemd_sources,
    dependencies: meson.get_compiler('c').find_library('dbus-1'),
    include_directories: inc_dirs,
    c_args: dbus_c_args,
    install: false,
)

lib_systemd_h = custom_target('libsystemd_service_impl.h',
    input: libsystemd_service_impl,
    output: 'libsystemd_service_impl.h',
    command: ['xxd', '-i', '-n', 'systemd_service_impl_bin', '@INPUT@', '@OUTPUT@'],
    build_by_default: true
)

launcher_exe = executable('bob-launcher',
    'src/C/systemd_service.h',
    'src/C/systemd_service.c',
    'src/C/systemd_service_utils.h',
    'src/C/systemd_service_utils.c',
    'src/main.c',
    lib_systemd_h,
    include_directories: include_directories('src/C'),
    install: true,
    install_dir: get_option('bindir'),
    install_rpath: join_paths(get_option('prefix'), get_option('libdir')),
    build_rpath: meson.current_build_dir()
)

schema_dir = join_paths(meson.current_build_dir(), 'data', 'glib-2.0', 'schemas')
run_command('mkdir', '-p', schema_dir, check: true)

compile_schemas = custom_target('glib-compile-schemas',
    build_by_default: true,
    output: 'gschemas.compiled',
    command: [
        find_program('glib-compile-schemas'),
        '--strict',
        schema_dir
    ]
)

schema_file = meson.project_name() + '.gschema.xml'
schema_target = custom_target(meson.project_name() + '-schemas',
    input: join_paths('data', schema_file),
    output: schema_file,
    command: ['cp', '@INPUT@', schema_dir],
    build_by_default: true
)

install_data(
    join_paths('data', schema_file),
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

gnome.post_install(
    glib_compile_schemas: true,
    update_desktop_database: true,
)
