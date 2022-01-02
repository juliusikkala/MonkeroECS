project(
  'monkeroecs-examples',
  ['cpp'],
  default_options : [
    'cpp_std=c++17',
    'buildtype=debugoptimized',
    'warning_level=3',
  ]
)

doxygen = find_program('doxygen', required: false)
if doxygen.found()
  config = configuration_data()
  config.set('WORKDIR', meson.source_root())

  doxyfile = configure_file(
    input: 'Doxyfile.in',
    output: 'Doxyfile',
    configuration: config
  )
  run_target('docs', command : [doxygen, doxyfile])
endif

incdir = include_directories('.')

executable(
  'boardgame',
  files('examples/boardgame.cc'),
  include_directories: [incdir],
  install: true,
)

executable(
  'everything',
  files('examples/everything.cc'),
  include_directories: [incdir],
  install: true,
)

executable(
  'bench',
  files('examples/benchmark.cc'),
  include_directories: [incdir],
  install: true,
)

test('events', executable('events', 'tests/events.cc', include_directories: [incdir]))
test('entities', executable('entities', 'tests/entities.cc', include_directories: [incdir]))
test('components', executable('components', 'tests/components.cc', include_directories: [incdir]))
test('foreach', executable('foreach', 'tests/foreach.cc', include_directories: [incdir]))
test('search', executable('search', 'tests/search.cc', include_directories: [incdir]))
