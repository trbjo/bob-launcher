project('io.github.trbjo.bob.launcher', 'vala', 'c',
    version: '0.5.0',
    default_options: [
        'buildtype=release',
        'b_lto=true',
        'prefix=/usr/local'
    ]
)

settings_dep = subproject('gtk4-settings-library').get_variable('settings_dep')

simple_keyboard = subproject('simple-keyboard')
simple_keyboard_dep = simple_keyboard.get_variable('simple_keyboard_dep')
simple_keyboard_vapi_dir = simple_keyboard.get_variable('simple_keyboard_dep').get_variable('vapi_dir')

thread_manager = subproject('thread-manager')
thread_manager_dep = thread_manager.get_variable('thread_manager_dep')
thread_manager_vapi_dir = thread_manager.get_variable('thread_manager_dep').get_variable('vapi_dir')

lib_sources = files(
    'src/api/interfaces.vala',
    'src/core/appsettings.vala',
    'src/core/constants.vala',
    'src/core/data-sink.vala',
    'src/core/match/file-match.vala',
    'src/core/match/match.vala',
    'src/core/match/unknown-match.vala',
    'src/core/plugin/plugin-base.vala',
    'src/core/plugin/plugin-loader.vala',
    'src/core/utils.vala',
    'src/gui/drag-and-drop-handler.vala',
    'src/gui/icon-cache-service.vala',
    'src/gui/css.vala',
    'src/gui/input-region.vala',
    'src/gui/scroll-controller.vala',
    'src/gui/drag-controller.vala',
    'src/gui/utils.vala',
    'src/gui/layershell.vala',
    'src/main.vala',
    'src/gui/widgets/launcher-window.vala',
    'src/gui/widgets/main-container.vala',
    'src/gui/widgets/match-row.vala',
    'src/gui/widgets/row-number.vala',
    'src/gui/widgets/match-row-label.vala',
    'src/gui/widgets/text-image.vala',
    'src/gui/widgets/query-container.vala',
    'src/gui/widgets/resize-handle.vala',
    'src/gui/widgets/width-resize-handle.vala',
    'src/gui/widgets/result_box.vala',
)

c_sources = files(
    'src/C/data-sink-actions.c',
    'src/C/data-sink-sources.c',
    'src/C/state.c',
    'src/C/controller.c',
    'src/C/keybindings.c',
    'src/core/search/result-container.c',
    'src/C/highlight.c',
    'src/C/events.c',
    'src/C/fzy/match.c',
    'src/C/string-utils.c',
    'src/C/fzy/hashset.c',
    'src/C/fzy/config.h',
)

gnome = import('gnome')

gresources = gnome.compile_resources(
    'gresources',
    join_paths('data', meson.project_name() + '.gresource.xml'),
    source_dir: 'data',
)

inc_dirs = include_directories(
    'src/core/search',
    'src/C/fzy',
    'src/C',
)

launcher_lib = shared_library('bob-launcher',
    lib_sources + c_sources + gresources,
    dependencies: [
        dependency('gtk4-layer-shell-0', version: '>= 0.6.0'),
        dependency('gio-unix-2.0'),
        dependency('glib-2.0', version: '>= 2.66.0'),
        dependency('gmodule-2.0'),
        dependency('gtk4', version: '>= 4.0.0'),
        dependency('gtk4-wayland', version: '>= 4.0.0'),
        dependency('wayland-client'),
        settings_dep,
        simple_keyboard_dep,
        thread_manager_dep,
        meson.get_compiler('c').find_library('m')
    ],
    include_directories: inc_dirs,
    vala_args: [
        '--vapidir=' + join_paths(meson.current_source_dir(), 'vapi'),
        '--pkg', 'c-bindings',
        '--pkg', 'state',
        '--pkg', 'keybindings',
        '--pkg', 'controller',
        '--pkg', 'result-container',
        '--pkg', 'data-sink-actions',
        '--pkg', 'levensteihn',
        '--pkg', 'string-utils',
        '--pkg', 'highlight',
        '--pkg', 'hashset',
        '--vapidir=' + thread_manager_vapi_dir,
        '--pkg', 'thread-manager',
        '--vapidir=' + simple_keyboard_vapi_dir,
        '--pkg', 'simple-keyboard'
    ],
    install: false,
)

stripped_lib = custom_target(
    'stripped_lib',
    input: launcher_lib,
    output: 'launcher.bin',
    command: get_option('buildtype') != 'debug' ? ['strip', '-o', '@OUTPUT@', '@INPUT@'] : ['cp', '@INPUT@', '@OUTPUT@'],
    build_by_default: true
)

liblauncher_h = custom_target('liblauncher.h',
    input: stripped_lib,
    output: 'liblauncher.h',
    command: ['xxd', '-i', '@INPUT@', '@OUTPUT@'],
    build_by_default: true
)

launcher_exe = executable('bob-launcher',
    'src/main.c',
    liblauncher_h,
    dependencies: [dependency('dbus-1')],
    install: false,
)

custom_target(
    'strip-' + launcher_exe.name(),
    input: launcher_exe,
    output: launcher_exe.name() + '.stripped',
    command: ['strip', '-s', '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true
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
