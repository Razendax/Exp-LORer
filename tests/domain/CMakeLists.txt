add_executable(explorer_domain_tests
    ResultTest.cpp
    FileNodeTest.cpp
    TagTest.cpp
    MediaMetadataTest.cpp
    FileTagAssociationTest.cpp
)

target_link_libraries(explorer_domain_tests PRIVATE explorer_domain GTest::gtest GTest::gtest_main)

gtest_discover_tests(explorer_domain_tests)
