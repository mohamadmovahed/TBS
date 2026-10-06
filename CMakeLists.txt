cmake_minimum_required(VERSION 3.16)

project(TbsReceiver
    VERSION 0.1.0
    DESCRIPTION "Qt receiver for TBS 6903x DVB-S2/S2X cards: TS / BBFrame capture to file and ZeroMQ"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TBS_ENABLE_ZMQ    "Enable ZeroMQ output (needs libzmq)"           ON)
option(TBS_ENABLE_FFMPEG "Enable video preview (needs FFmpeg libraries)" ON)
option(TBS_BUILD_TESTS   "Build unit tests"                               ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
message(STATUS "Using Qt ${QT_VERSION}")

# --- Optional dependencies -------------------------------------------------
if(TBS_ENABLE_ZMQ)
    include(FindZeroMQLib)
    if(NOT TARGET tbs::zmq)
        message(WARNING "libzmq not found - ZeroMQ output disabled (set TBS_ENABLE_ZMQ=OFF to silence)")
        set(TBS_ENABLE_ZMQ OFF)
    endif()
endif()

if(TBS_ENABLE_FFMPEG)
    include(FindFFmpegLibs)
    if(NOT TARGET tbs::ffmpeg)
        message(WARNING "FFmpeg not found - video preview disabled (set TBS_ENABLE_FFMPEG=OFF to silence)")
        set(TBS_ENABLE_FFMPEG OFF)
    endif()
endif()

# --- Core library (no GUI) ------------------------------------------------
add_library(tbscore STATIC
    src/core/TunerConfig.cpp
    src/core/TunerConfig.h
    src/core/DvbNames.cpp
    src/core/DvbNames.h
    src/core/FrontendStatus.h
    src/core/StreamStats.h
    src/core/PlsUtil.cpp
    src/core/PlsUtil.h
    src/core/TsAnalyzer.cpp
    src/core/TsAnalyzer.h
    src/core/BbHeader.cpp
    src/core/BbHeader.h
    src/core/BbFrameExtractor.cpp
    src/core/BbFrameExtractor.h
    src/core/BbFrameToTs.cpp
    src/core/BbFrameToTs.h
    src/core/ITunerDevice.h
    src/core/FileReplayDevice.cpp
    src/core/FileReplayDevice.h
    src/core/DeviceFactory.cpp
    src/core/DeviceFactory.h
    src/core/ReceiverEngine.cpp
    src/core/ReceiverEngine.h
    src/core/ITsConsumer.h
    src/output/IOutputSink.h
    src/output/FileSink.cpp
    src/output/FileSink.h
    src/output/ZmqSink.cpp
    src/output/ZmqSink.h
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tbscore PRIVATE src/core/LinuxDvbDevice.cpp src/core/LinuxDvbDevice.h)
    target_compile_definitions(tbscore PUBLIC TBS_HAVE_LINUX_DVB=1)
endif()
target_include_directories(tbscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tbscore PUBLIC Qt${QT_VERSION_MAJOR}::Core)
if(TBS_ENABLE_ZMQ)
    target_link_libraries(tbscore PRIVATE tbs::zmq)
    target_compile_definitions(tbscore PUBLIC TBS_HAVE_ZMQ=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(tbscore PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(tbscore PRIVATE /W4 /permissive-)
    target_compile_definitions(tbscore PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(tbscore PRIVATE -Wall -Wextra)
endif()

# --- GUI application ---------------------------------------------------------
add_executable(tbs-receiver
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/TuningPanel.cpp
    src/ui/TuningPanel.h
    src/ui/OutputPanel.cpp
    src/ui/OutputPanel.h
    src/ui/StatusPanel.cpp
    src/ui/StatusPanel.h
    src/ui/LedIndicator.cpp
    src/ui/LedIndicator.h
    src/video/VideoWidget.cpp
    src/video/VideoWidget.h
)
if(TBS_ENABLE_FFMPEG)
    target_sources(tbs-receiver PRIVATE src/video/VideoDecoder.cpp src/video/VideoDecoder.h)
    target_link_libraries(tbs-receiver PRIVATE tbs::ffmpeg)
    target_compile_definitions(tbs-receiver PRIVATE TBS_HAVE_FFMPEG=1)
endif()
target_link_libraries(tbs-receiver PRIVATE tbscore
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(tbs-receiver PRIVATE TBS_APP_VERSION="${PROJECT_VERSION}")
set_target_properties(tbs-receiver PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)

# --- Command line tool to generate test streams -------------------------------
add_executable(tbs-make-test-stream tools/make_test_stream.cpp tests/StreamBuilders.h)
target_link_libraries(tbs-make-test-stream PRIVATE tbscore)

# --- Tests ---------------------------------------------------------------------
if(TBS_BUILD_TESTS)
    enable_testing()
    add_executable(tbs-tests tests/test_core.cpp tests/StreamBuilders.h)
    target_link_libraries(tbs-tests PRIVATE tbscore)
    add_test(NAME tbs-core-tests COMMAND tbs-tests)
endif()

install(TARGETS tbs-receiver tbs-make-test-stream
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)

message(STATUS "ZeroMQ output : ${TBS_ENABLE_ZMQ}")
message(STATUS "Video preview: ${TBS_ENABLE_FFMPEG}")
