add_executable(ExpLORer
    main.cpp
    CompositionRoot.h
    CompositionRoot.cpp
)

target_link_libraries(ExpLORer PRIVATE explorer_ui_widgets Qt6::Widgets)
