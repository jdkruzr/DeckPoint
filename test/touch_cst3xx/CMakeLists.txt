add_executable(TouchCst3xxTest TouchCst3xxTest.cpp)

target_include_directories(TouchCst3xxTest PRIVATE ../../freeink-sdk/libs/hardware/InputManager/include)

target_link_libraries(TouchCst3xxTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TouchCst3xxTest)
