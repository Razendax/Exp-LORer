add_executable(explorer_adapters_tests
    TestMain.cpp
    NavigationHistoryTest.cpp
    StandardFileSystemRepositoryTest.cpp
    DriveLabelTest.cpp
    WorkspaceLayoutTopologyTest.cpp
    SQLiteTagRepositoryTest.cpp
    AppConfigStoreTest.cpp
    WorkspaceControllerTest.cpp
    LoggingTest.cpp
    AnsiTerminalBufferTest.cpp
    ThumbnailCacheTest.cpp
    CachingMediaDecoderTest.cpp
    VipsImageDecoderTest.cpp
    FFmpegMediaDecoderTest.cpp
    SyntaxHighlightEngineTest.cpp
    HighlightThemeTest.cpp
    ArchivePathResolverTest.cpp
    LibArchiveReaderTest.cpp
    ArchiveIndexCacheTest.cpp
)

target_link_libraries(explorer_adapters_tests PRIVATE
    explorer_adapters_viewmodels
    explorer_adapters_filesystem
    explorer_adapters_persistence
    explorer_adapters_shell
    explorer_adapters_config
    explorer_adapters_logging
    explorer_adapters_terminal
    explorer_adapters_media
    explorer_adapters_highlighting
    explorer_adapters_archive
    Qt6::Core
    GTest::gtest
    GTest::gmock
)

gtest_discover_tests(explorer_adapters_tests)

# SQLiteTagRepositoryTest needs the SQLite driver plugin deployed next to the test binary, same
# gap as the app's own qwindows.dll deployment in src/app/CMakeLists.txt.
if(WIN32 AND TARGET Qt6::QSQLiteDriverPlugin)
    add_custom_command(TARGET explorer_adapters_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:explorer_adapters_tests>/sqldrivers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>"
                "$<TARGET_FILE_DIR:explorer_adapters_tests>/sqldrivers/"
        COMMENT "Deploying Qt SQLite driver plugin (qsqlite.dll)"
    )
endif()
