project('momentum-anc', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=1.5',
  default_options: [
    'c_std=c23',
    'warning_level=3',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

# libbluetooth ships no .pc file, so find the library by name.
# Headers come from bluez-libs-devel in the default include path.
bluetooth = cc.find_library('bluetooth', required: true)

# needed for the GUI
gtk4 = dependency('gtk4', version: '>=4.16')
adwaita = dependency('libadwaita-1', version: '>=1.6')

subdir('src')
