add_executable(TouchPolicyTest TouchPolicyTest.cpp ReaderTouchTest.cpp)

target_include_directories(TouchPolicyTest PRIVATE
  ../../src
  ../../freeink-sdk/libs/hardware/InputManager/include
)

target_link_libraries(TouchPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TouchPolicyTest)
