cmake_minimum_required(VERSION 3.16)
project(sni-relay-manager VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(sni-relay-manager
    resources.qrc
    src/main.cpp
    src/mainwindow.cpp
    src/generator.cpp
    src/sshutil.cpp
)

target_link_libraries(sni-relay-manager PRIVATE Qt6::Widgets)

install(TARGETS sni-relay-manager RUNTIME DESTINATION bin)
