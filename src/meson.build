gaia_lib = static_library('gaia', 'gaia.c')

app_lib = static_library('app',
  ['bt.c', 'session.c', 'ui.c'],
  dependencies: [bluetooth, gtk4, adwaita],
  link_with: gaia_lib,
)

test_gaia = executable('test-gaia', 'test-gaia.c', link_with: gaia_lib)
test('gaia codec', test_gaia)

test_ui = executable('test-ui', 'test-ui.c',
  dependencies: [bluetooth, gtk4, adwaita],
  link_with: [app_lib, gaia_lib],
)

# test-ui needs a display; without xvfb it can still be run by hand.
xvfb = find_program('xvfb-run', required: false)
if xvfb.found()
  test('ui without a session', xvfb, args: ['-a', test_ui], timeout: 60)
endif

valgrind = find_program('valgrind', required: false)
if valgrind.found()
  memcheck = ['--error-exitcode=1', '--leak-check=full',
              '--errors-for-leak-kinds=definite', '--quiet']
  test('gaia codec (valgrind)', valgrind, args: memcheck + [test_gaia],
    suite: 'valgrind', timeout: 300)
  if xvfb.found()
    test('ui (valgrind)', xvfb,
      args: ['-a', valgrind] + memcheck + [test_ui], suite: 'valgrind',
      timeout: 600)
  endif
endif

executable('momentum-anc', 'main.c',
  dependencies: [bluetooth, gtk4, adwaita],
  link_with: [app_lib, gaia_lib],
  install: true,
)
