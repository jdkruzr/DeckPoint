add_executable(ToneCurveTest
  ToneCurveTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/ToneCurve.cpp
)

target_link_libraries(ToneCurveTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ToneCurveTest)
