find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_subdirectory(domain)
add_subdirectory(application)
add_subdirectory(adapters)
