cmake_minimum_required(VERSION 3.19)
project(SimpleUpdater VERSION 0.2.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

qt_standard_project_setup()

configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h")
configure_file(version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/updatecontroller.h src/updatecontroller.cpp
    src/filehandler.h src/filehandler.cpp
    src/cliparser.h src/cliparser.cpp
    src/manifest.h src/manifest.cpp
    src/downloadhandler.h src/downloadhandler.cpp
)
if(WIN32)
    list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
    set(PLATFORM_SRC src/platform/win32.cpp)
    set(PLATFORM_LIBS Version Rstrtmgr)
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_SRC src/platform/linux.cpp)
endif()

qt_add_executable(SimpleUpdater
    WIN32 MACOSX_BUNDLE
    ${SOURCES}
    ${PLATFORM_SRC}
)

target_include_directories(SimpleUpdater PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(SimpleUpdater
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        ${PLATFORM_LIBS}
)

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

install(TARGETS SimpleUpdater
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET SimpleUpdater
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
