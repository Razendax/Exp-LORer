add_executable(ExpLORer
    main.cpp
    CompositionRoot.h
    CompositionRoot.cpp
)

target_link_libraries(ExpLORer PRIVATE explorer_ui_widgets Qt6::Widgets)

# Qt's platform plugin (qwindows.dll) is loaded dynamically at runtime from a
# "platforms" subfolder next to the executable. vcpkg's applocal DLL copying
# only handles directly-linked DLLs, so it never picks this up; deploy it
# explicitly or the app fails immediately with "no Qt platform plugin".
if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    add_custom_command(TARGET ExpLORer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ExpLORer>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:ExpLORer>/platforms/"
        COMMENT "Deploying Qt platform plugin (qwindows.dll)"
    )
endif()
