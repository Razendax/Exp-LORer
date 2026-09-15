add_executable(explorer_application_tests
    TestMain.cpp
    MockFileSystemRepository.h
    MockContextMenuProvider.h
    MockTagRepository.h
    MockMediaDecoder.h
    FileNavigationUseCaseTest.cpp
    TagManagementUseCaseTest.cpp
    MediaProcessingUseCaseTest.cpp
)

target_link_libraries(explorer_application_tests PRIVATE
    explorer_application
    GTest::gtest
    GTest::gmock
)

gtest_discover_tests(explorer_application_tests)
