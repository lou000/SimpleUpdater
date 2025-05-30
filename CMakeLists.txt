cmake_minimum_required(VERSION 3.19)
project(SimpleUpdater LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
set(Qt6_DIR "C:/Qt/static/lib/cmake/Qt6")

qt_standard_project_setup()

set(SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    filehandler.h filehandler.cpp
    desktopshortcut.h
)
if(WIN32)
    list(APPEND SOURCES
        desktopshortcut_win.h
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND SOURCES
        desktopshortcut_linux.h
    )
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DDEBUG")
qt_add_executable(SimpleUpdater
    WIN32 MACOSX_BUNDLE
    ${SOURCES}
)

target_link_libraries(SimpleUpdater
    PRIVATE
        Qt::Core
        Qt::Widgets
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
