gtest = dependency('gtest', main: true, required: false)
if gtest.found()
  test('unit', executable('unit-tests', 'pipeline_test.cpp', dependencies: [core_dep, gtest]))
endif
