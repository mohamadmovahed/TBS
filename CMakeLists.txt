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
option(TBS_BUILD_DAEMON "Build tbs-daemon (headless receiver with REST/WebSocket API)" ON)
option(TBS_BUILD_PYTHON "Build the Python module (needs pybind11 and Python development files)" ON)

if(TBS_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development.Module QUIET)
    if(Python_FOUND)
        set(PYBIND11_FINDPYTHON ON)
        find_package(pybind11 CONFIG QUIET)
    endif()
    if(Python_FOUND AND pybind11_FOUND)
        # The static libraries go into a shared module
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        message(STATUS "Python module: ON (Python ${Python_VERSION})")
    else()
        message(STATUS "Python module: OFF (needs pybind11 and the Python development files)")
        set(TBS_BUILD_PYTHON OFF)
    endif()
endif()
option(TBS_ENABLE_FFMPEG "Video preview in the GUI (needs FFmpeg)" ON)
option(TBS_SANITIZE     "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(TBS_BUILD_FUZZERS "Build the fuzz targets (libFuzzer with Clang; corpus replay otherwise)" OFF)

if(TBS_SANITIZE OR (TBS_BUILD_FUZZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
        add_link_options(-fsanitize=address,undefined)
        if(TBS_BUILD_FUZZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-fsanitize=fuzzer-no-link) # coverage for the libraries
        endif()
    endif()
endif()

# ---- The library (no Qt) ----
add_subdirectory(libtbsdvb)
# ---- BBFrame / TS content processing (no Qt, no hardware) ----
add_subdirectory(libbbproc)

# ---- Command line tools ----
if(TBS_BUILD_TOOLS)
    add_executable(tbs-cli tools/tbs_cli.cpp)
    target_link_libraries(tbs-cli PRIVATE TbsDvb::tbsdvb TbsDvb::tbsbbproc)

    add_executable(tbs-make-test-stream tools/make_test_stream.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-make-test-stream PRIVATE TbsDvb::tbsdvb)

    add_executable(tbs-bbproc tools/bbproc_tool.cpp)
    target_link_libraries(tbs-bbproc PRIVATE TbsDvb::tbsbbproc)

    add_executable(tbs-c-example tools/c_api_example.c)
    target_link_libraries(tbs-c-example PRIVATE TbsDvb::tbsdvb)
    set_target_properties(tbs-c-example PROPERTIES LINKER_LANGUAGE CXX)

    install(TARGETS tbs-cli tbs-make-test-stream tbs-bbproc RUNTIME DESTINATION bin)
endif()

# ---- Headless daemon ----
if(TBS_BUILD_DAEMON)
    add_subdirectory(daemon)
endif()

# ---- Tests ----
if(TBS_BUILD_TESTS)
    enable_testing()
    add_executable(tbs-tests tests/test_core.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-tests PRIVATE TbsDvb::tbsdvb TbsDvb::tbsbbproc)
    # White-box tests use the library's private headers; a shared build hides those symbols
    target_include_directories(tbs-tests PRIVATE libtbsdvb/src)
    if(WIN32)
        target_link_libraries(tbs-tests PRIVATE ws2_32)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TBSDVB_BUILD_SHARED)
        target_compile_definitions(tbs-tests PRIVATE TBS_TEST_LINUX_INTERNALS=1)
    endif()
    add_test(NAME tbsdvb-core COMMAND tbs-tests)

    add_executable(tbs-bbproc-tests tests/test_bbproc.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-bbproc-tests PRIVATE TbsDvb::tbsbbproc)
    add_test(NAME tbsbbproc COMMAND tbs-bbproc-tests)

    if(TBS_BUILD_DAEMON)
        add_executable(tbs-daemon-tests tests/test_daemon.cpp tests/StreamBuilders.h)
        target_link_libraries(tbs-daemon-tests PRIVATE tbs-daemon-core)
        add_test(NAME tbs-daemon COMMAND tbs-daemon-tests)
    endif()
endif()

# ---- Fuzzing ----
if(TBS_BUILD_FUZZERS)
    enable_testing()
    add_subdirectory(fuzz)
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
message(STATUS "tbsdvb SRT    : ${TBSDVB_ENABLE_SRT}")

# ---- Python module (after enable_testing, it registers a test) ----
if(TBS_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# ---- Installers (cpack) ----
include(Packaging)
