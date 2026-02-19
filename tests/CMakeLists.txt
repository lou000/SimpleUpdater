find_package(Qt6 REQUIRED COMPONENTS Core Test Widgets)

set(CMAKE_AUTOMOC ON)

if(MSVC)
    get_target_property(_qt_core_type Qt6::Core TYPE)
    if(_qt_core_type STREQUAL "STATIC_LIBRARY")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

if(WIN32)
    set(TEST_PLATFORM_SRC ${CMAKE_SOURCE_DIR}/src/platform/win32.cpp)
    set(TEST_PLATFORM_LIBS Version Rstrtmgr)
elseif(UNIX AND NOT APPLE)
    set(TEST_PLATFORM_SRC ${CMAKE_SOURCE_DIR}/src/platform/linux.cpp)
endif()

function(add_unit_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt::Core Qt::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(tst_manifest ${CMAKE_SOURCE_DIR}/src/manifest.cpp ${TEST_PLATFORM_SRC})
target_link_libraries(tst_manifest PRIVATE ${TEST_PLATFORM_LIBS})

add_unit_test(tst_filehandler ${CMAKE_SOURCE_DIR}/src/filehandler.cpp ${TEST_PLATFORM_SRC})
target_link_libraries(tst_filehandler PRIVATE ${TEST_PLATFORM_LIBS})

add_unit_test(tst_cliparser ${CMAKE_SOURCE_DIR}/src/cliparser.cpp ${TEST_PLATFORM_SRC})
target_link_libraries(tst_cliparser PRIVATE Qt::Widgets ${TEST_PLATFORM_LIBS})
