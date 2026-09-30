cmake_minimum_required(VERSION 3.16)
project(sni-relay-manager VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)

add_executable(sni-relay-manager
    resources.qrc
    src/main.cpp
    src/mainwindow.cpp
    src/generator.cpp
    src/sshutil.cpp
    src/qr.cpp
)

target_link_libraries(sni-relay-manager PRIVATE Qt6::Widgets PkgConfig::QRENCODE)

install(TARGETS sni-relay-manager RUNTIME DESTINATION bin)
