add_executable(Exp-LORer
    main.cpp
    CompositionRoot.h
    CompositionRoot.cpp
    ExceptionHandler.h
    ExceptionHandler.cpp
    ExpLorerApplication.h
    ExpLorerApplication.cpp
)

target_link_libraries(Exp-LORer PRIVATE
    explorer_ui_widgets
    explorer_adapters_filesystem
    explorer_adapters_persistence
    explorer_adapters_shell
    explorer_adapters_config
    explorer_adapters_logging
    explorer_adapters_viewmodels
    explorer_application
    Qt6::Widgets
)

# DbgHelp symbolizes the fatal-exception call stack logged by ExceptionHandler (Architecture.md §14.22).
if(WIN32)
    target_link_libraries(Exp-LORer PRIVATE Dbghelp)
endif()

# Qt's platform plugin (qwindows.dll) is loaded dynamically at runtime from a
# "platforms" subfolder next to the executable. vcpkg's applocal DLL copying
# only handles directly-linked DLLs, so it never picks this up; deploy it
# explicitly or the app fails immediately with "no Qt platform plugin".
if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    add_custom_command(TARGET Exp-LORer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Exp-LORer>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:Exp-LORer>/platforms/"
        COMMENT "Deploying Qt platform plugin (qwindows.dll)"
    )
endif()

# Same applocal-DLL-copy gap as qwindows.dll above: QSqlDatabase loads the SQLite driver
# (SQLiteTagRepository) from a "sqldrivers" subfolder next to the executable at runtime.
if(WIN32 AND TARGET Qt6::QSQLiteDriverPlugin)
    add_custom_command(TARGET Exp-LORer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Exp-LORer>/sqldrivers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>"
                "$<TARGET_FILE_DIR:Exp-LORer>/sqldrivers/"
        COMMENT "Deploying Qt SQLite driver plugin (qsqlite.dll)"
    )
endif()
