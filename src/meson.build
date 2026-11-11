gaia_lib = static_library('gaia', 'gaia.c')

test_gaia = executable('test-gaia', 'test-gaia.c', link_with: gaia_lib)
test('gaia codec', test_gaia)

executable('momentum-anc',
  ['main.c', 'ui.c', 'session.c', 'bt.c'],
  dependencies: [bluetooth, gtk4, adwaita],
  link_with: gaia_lib,
  install: true,
)
