cmake_minimum_required(VERSION 3.19)
project(SimpleUpdater VERSION 0.2.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

qt_standard_project_setup()

configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h")
configure_file(version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

set(SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    filehandler.h filehandler.cpp
    cliparser.h cliparser.cpp
    desktopshortcut.h
)
if(WIN32)
    list(APPEND SOURCES
        desktopshortcut_win.h
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    )
    set(PLATFORM_SRC platform/win32.cpp)
    set(PLATFORM_LIBS Version)
elseif(UNIX AND NOT APPLE)
    list(APPEND SOURCES
        desktopshortcut_linux.h
    )
    set(PLATFORM_SRC platform/linux.cpp)
endif()

qt_add_executable(SimpleUpdater
    WIN32 MACOSX_BUNDLE
    ${SOURCES}
    ${PLATFORM_SRC}
)

target_include_directories(SimpleUpdater PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(SimpleUpdater
    PRIVATE
        Qt::Core
        Qt::Widgets
        ${PLATFORM_LIBS}
)

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
