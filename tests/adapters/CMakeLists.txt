add_executable(explorer_adapters_tests
    NavigationHistoryTest.cpp
    StandardFileSystemRepositoryTest.cpp
)

target_link_libraries(explorer_adapters_tests PRIVATE
    explorer_adapters_viewmodels
    explorer_adapters_filesystem
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(explorer_adapters_tests)
