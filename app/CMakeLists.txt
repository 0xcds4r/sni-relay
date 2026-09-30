cmake_minimum_required(VERSION 3.16)
project(sni-relay-manager VERSION 1.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SOURCES
    resources.qrc
    src/main.cpp
    src/mainwindow.cpp
    src/generator.cpp
    src/sshutil.cpp
    src/qr.cpp
)

if(WIN32)
    list(APPEND SOURCES third_party/qrcodegen/qrcodegen.cpp)
endif()

add_executable(sni-relay-manager ${SOURCES})

target_link_libraries(sni-relay-manager PRIVATE Qt6::Widgets)

if(WIN32)
    # На Windows — встроенный qrcodegen (без внешних зависимостей).
    target_include_directories(sni-relay-manager PRIVATE third_party/qrcodegen)
    target_compile_definitions(sni-relay-manager PRIVATE SRM_USE_QRCODEGEN)
    target_link_options(sni-relay-manager PRIVATE -mwindows)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)
    target_link_libraries(sni-relay-manager PRIVATE PkgConfig::QRENCODE)
endif()

install(TARGETS sni-relay-manager RUNTIME DESTINATION bin)
