# tbsdvb - TBS DVB-S2/S2X receiver library (pure C++17, no GUI dependencies)

option(TBSDVB_BUILD_SHARED "Build tbsdvb as a shared library" OFF)
option(TBSDVB_ENABLE_ZMQ "ZeroMQ output sink (needs libzmq)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(TBSDVB_PUBLIC_HEADERS
    include/tbsdvb/export.h
    include/tbsdvb/tbsdvb.h
    include/tbsdvb/tbsdvb_c.h
    include/tbsdvb/Types.h
    include/tbsdvb/Status.h
    include/tbsdvb/Device.h
    include/tbsdvb/Sink.h
    include/tbsdvb/Receiver.h
    include/tbsdvb/BbHeader.h
    include/tbsdvb/BbFrameExtractor.h
    include/tbsdvb/BbFrameToTs.h
    include/tbsdvb/TsAnalyzer.h
    include/tbsdvb/PlsUtil.h
    include/tbsdvb/DvbNames.h
)

set(TBSDVB_SOURCES
    src/Util.cpp
    src/Util.h
    src/Types.cpp
    src/BbHeader.cpp
    src/BbFrameExtractor.cpp
    src/BbFrameToTs.cpp
    src/TsAnalyzer.cpp
    src/PlsUtil.cpp
    src/DvbNames.cpp
    src/Devices.cpp
    src/FileReplayDevice.cpp
    src/FileReplayDevice.h
    src/FileSink.cpp
    src/ZmqSink.cpp
    src/Receiver.cpp
    src/CApi.cpp
)

if(TBSDVB_BUILD_SHARED)
    add_library(tbsdvb SHARED ${TBSDVB_SOURCES} ${TBSDVB_PUBLIC_HEADERS})
    set_target_properties(tbsdvb PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
else()
    add_library(tbsdvb STATIC ${TBSDVB_SOURCES} ${TBSDVB_PUBLIC_HEADERS})
    target_compile_definitions(tbsdvb PUBLIC TBSDVB_STATIC)
endif()
add_library(TbsDvb::tbsdvb ALIAS tbsdvb)

target_compile_features(tbsdvb PUBLIC cxx_std_17)
target_compile_definitions(tbsdvb PRIVATE TBSDVB_BUILDING TBSDVB_VERSION="${PROJECT_VERSION}")
target_include_directories(tbsdvb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(tbsdvb PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(tbsdvb PUBLIC Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tbsdvb PRIVATE src/platform/LinuxDvbDevice.cpp src/platform/LinuxDvbDevice.h)
    target_compile_definitions(tbsdvb PRIVATE TBSDVB_HAVE_LINUX_DVB=1)
elseif(WIN32)
    target_sources(tbsdvb PRIVATE src/platform/WinBdaDevice.cpp src/platform/WinBdaDevice.h)
    target_compile_definitions(tbsdvb PRIVATE TBSDVB_HAVE_WIN_BDA=1 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(tbsdvb PRIVATE strmiids ole32 oleaut32 uuid)
endif()

if(TBSDVB_ENABLE_ZMQ)
    include(FindZeroMQLib)
    if(TARGET tbs::zmq)
        target_link_libraries(tbsdvb PRIVATE tbs::zmq)
        target_compile_definitions(tbsdvb PRIVATE TBSDVB_HAVE_ZMQ=1)
    else()
        message(WARNING "libzmq not found - ZeroMQ output disabled (set TBSDVB_ENABLE_ZMQ=OFF to silence)")
        set(TBSDVB_ENABLE_ZMQ OFF)
    endif()
endif()
set(TBSDVB_ENABLE_ZMQ ${TBSDVB_ENABLE_ZMQ} PARENT_SCOPE)

if(MSVC)
    target_compile_options(tbsdvb PRIVATE /W4 /wd4251 /permissive-)
else()
    target_compile_options(tbsdvb PRIVATE -Wall -Wextra)
endif()

# ---- Install / package --------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS tbsdvb EXPORT TbsDvbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/tbsdvb DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TbsDvbTargets NAMESPACE TbsDvb:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TbsDvb)
configure_package_config_file(cmake/TbsDvbConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TbsDvbConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TbsDvb)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/TbsDvbConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TbsDvbConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TbsDvbConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TbsDvb)
