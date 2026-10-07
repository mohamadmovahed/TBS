cmake_minimum_required(VERSION 3.16)

project(TbsReceiver
    VERSION 0.2.0
    DESCRIPTION "TBS 6903x DVB-S2/S2X receiver: tbsdvb library, CLI and Qt GUI"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TBS_BUILD_GUI    "Build the Qt GUI application (tbs-receiver)" ON)
option(TBS_BUILD_TOOLS  "Build tbs-cli, tbs-make-test-stream and the C API example" ON)
option(TBS_BUILD_TESTS  "Build unit tests" ON)
option(TBS_ENABLE_FFMPEG "Video preview in the GUI (needs FFmpeg)" ON)

# ---- The library (no Qt) ----
add_subdirectory(libtbsdvb)

# ---- Command line tools ----
if(TBS_BUILD_TOOLS)
    add_executable(tbs-cli tools/tbs_cli.cpp)
    target_link_libraries(tbs-cli PRIVATE TbsDvb::tbsdvb)

    add_executable(tbs-make-test-stream tools/make_test_stream.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-make-test-stream PRIVATE TbsDvb::tbsdvb)

    add_executable(tbs-c-example tools/c_api_example.c)
    target_link_libraries(tbs-c-example PRIVATE TbsDvb::tbsdvb)
    set_target_properties(tbs-c-example PROPERTIES LINKER_LANGUAGE CXX)

    install(TARGETS tbs-cli tbs-make-test-stream RUNTIME DESTINATION bin)
endif()

# ---- Tests ----
if(TBS_BUILD_TESTS)
    enable_testing()
    add_executable(tbs-tests tests/test_core.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-tests PRIVATE TbsDvb::tbsdvb)
    # White-box tests use the library's private headers; a shared build hides those symbols
    target_include_directories(tbs-tests PRIVATE libtbsdvb/src)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TBSDVB_BUILD_SHARED)
        target_compile_definitions(tbs-tests PRIVATE TBS_TEST_LINUX_INTERNALS=1)
    endif()
    add_test(NAME tbsdvb-core COMMAND tbs-tests)
endif()

# ---- GUI ----
if(TBS_BUILD_GUI)
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Core)
    if(QT_FOUND)
        add_subdirectory(app)
    else()
        message(WARNING "Qt not found - GUI not built (the library and tools still are)")
    endif()
endif()

message(STATUS "tbsdvb ZeroMQ : ${TBSDVB_ENABLE_ZMQ}")
