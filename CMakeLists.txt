cmake_minimum_required(VERSION 3.24)
project(Exp-LORer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(spdlog CONFIG REQUIRED)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
