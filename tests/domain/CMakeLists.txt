add_executable(explorer_domain_tests
    ResultTest.cpp
    PathUtf8Test.cpp
    PathContainmentTest.cpp
    GlobMatchTest.cpp
    WholeWordMatchTest.cpp
    RegexMatchTest.cpp
    SubstringMatchTest.cpp
    BinaryContentDetectorTest.cpp
    FileNodeTest.cpp
    TagTest.cpp
    TagHierarchyTest.cpp
    MediaMetadataTest.cpp
    FileTagAssociationTest.cpp
    FilePreviewTest.cpp
    ErrorTest.cpp
    BulkRenamePatternTest.cpp
)

target_link_libraries(explorer_domain_tests PRIVATE explorer_domain GTest::gtest GTest::gtest_main)

gtest_discover_tests(explorer_domain_tests)
