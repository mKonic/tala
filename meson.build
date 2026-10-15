project('tala', 'cpp', 'c',
  version: 'git',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.3.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
  ],
)

qt6 = import('qt6')
qt = dependency('qt6', version: '>=6.8', modules: ['Core', 'Gui', 'Widgets', 'Qml', 'Quick', 'QuickControls2', 'DBus', 'Network'])
# LayerShellQt ships CMake files only; without CMake, its library directly.
layershellqt = dependency('LayerShellQt', method: 'cmake', modules: ['LayerShellQt::Interface'], required: false)
if not layershellqt.found()
  layershellqt = meson.get_compiler('cpp').find_library('LayerShellQtInterface')
endif
wayland_client = dependency('wayland-client')
xkbcommon = dependency('xkbcommon')
pipewire = dependency('libpipewire-0.3')
sqlite = dependency('sqlite3')

subdir('protocols')

version_h = custom_target('version.hpp',
  output: 'version.hpp',
  command: [files('scripts/version.sh'), 'header', '@OUTPUT@'],
  build_always_stale: true,
)

# The pieces with no window: tested on their own.
core = static_library('tala-core',
  'src/pipeline.cpp', 'src/store.cpp', 'src/wav.cpp',
  dependencies: [qt, sqlite],
)
core_dep = declare_dependency(link_with: core, dependencies: [qt, sqlite], include_directories: include_directories('src'))

moc = qt6.compile_moc(headers: ['src/controller.hpp', 'src/engine.hpp', 'src/recorder.hpp', 'src/shortcuts.hpp',
  'src/inserter.hpp', 'src/ui.hpp', 'src/context.hpp'], dependencies: qt)
resources = qt6.compile_resources(sources: 'qml/qml.qrc')

executable('tala',
  'src/main.cpp', 'src/controller.cpp', 'src/engine.cpp', 'src/recorder.cpp', 'src/shortcuts.cpp', 'src/inserter.cpp', 'src/context.cpp',
  moc, resources, version_h, protocol_sources,
  dependencies: [core_dep, layershellqt, wayland_client, xkbcommon, pipewire],
  install: true,
)

# The speech model in a process of its own: Qt Core only.
qtcore = dependency('qt6', version: '>=6.8', modules: ['Core'])
executable('tala-engine',
  'src/engine_main.cpp', 'src/wav.cpp', 'src/phonon/phonon.cpp', 'src/phonon/mel.cpp', 'src/phonon/hotwords.cpp',
  'src/phonon/install.cpp',
  dependencies: [qtcore, meson.get_compiler('cpp').find_library('dl'), dependency('libcurl'), dependency('libarchive')],
  cpp_args: ['-O3'],
  install: true,
)

subdir('tests')

install_data('data/io.github.mkonic.Tala.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/icons/io.github.mkonic.Tala.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
