add_executable(explorer_domain_tests
    PlaceholderTest.cpp
)

target_link_libraries(explorer_domain_tests PRIVATE GTest::gtest GTest::gtest_main)

gtest_discover_tests(explorer_domain_tests)
