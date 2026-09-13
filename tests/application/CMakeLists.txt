add_executable(explorer_application_tests
    PlaceholderTest.cpp
)

target_link_libraries(explorer_application_tests PRIVATE GTest::gtest GTest::gtest_main)

gtest_discover_tests(explorer_application_tests)
