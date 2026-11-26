# tbsbbproc - BBFrame / TS content processing (pure C++17, no GUI, no hardware access)
#   * stream content detection per ISI (TS, GSE, generic)
#   * MPEG-2 PSI / DVB SI table analysis
#   * GSE decapsulation, IP statistics, pcap export
#   * non-standard generic stream encapsulations (GSE dialects, TS/CRC-TS and raw IP in GS)
#   * capture file reader (TS, pseudo-TS, raw / 0xB8 BBFrames)

option(TBSBBPROC_BUILD_SHARED "Build tbsbbproc as a shared library" ${TBSDVB_BUILD_SHARED})

set(TBSBBPROC_PUBLIC_HEADERS
    include/tbsbbproc/export.h
    include/tbsbbproc/Processor.h
    include/tbsbbproc/NetworkScan.h
    include/tbsbbproc/PsiTypes.h
    include/tbsbbproc/TsTableAnalyzer.h
    include/tbsbbproc/Gse.h
    include/tbsbbproc/GenericDecoders.h
    include/tbsbbproc/IpStatistics.h
    include/tbsbbproc/PcapWriter.h
    include/tbsbbproc/CaptureFile.h
    include/tbsbbproc/DvbText.h
    include/tbsbbproc/SectionCollector.h
    include/tbsbbproc/Mpe.h
    include/tbsbbproc/TunWriter.h
)
set(TBSBBPROC_SOURCES
    src/DvbText.cpp
    src/PsiNames.cpp
    src/TsTableAnalyzer.cpp
    src/Gse.cpp
    src/GenericDecoders.cpp
    src/IpStatistics.cpp
    src/PcapWriter.cpp
    src/Processor.cpp
    src/CaptureFile.cpp
    src/Report.cpp
    src/Mpe.cpp
    src/Xmltv.cpp
    src/NetworkScan.cpp
    src/TunWriter.cpp
)

if(TBSBBPROC_BUILD_SHARED)
    add_library(tbsbbproc SHARED ${TBSBBPROC_SOURCES} ${TBSBBPROC_PUBLIC_HEADERS})
    set_target_properties(tbsbbproc PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
        VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
else()
    add_library(tbsbbproc STATIC ${TBSBBPROC_SOURCES} ${TBSBBPROC_PUBLIC_HEADERS})
    target_compile_definitions(tbsbbproc PUBLIC TBSBBPROC_STATIC)
endif()
add_library(TbsDvb::tbsbbproc ALIAS tbsbbproc)

target_compile_features(tbsbbproc PUBLIC cxx_std_17)
target_compile_definitions(tbsbbproc PRIVATE TBSBBPROC_BUILDING)
target_include_directories(tbsbbproc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Uses BbHeader / BbFrameToTs / TsAnalyzer from tbsdvb
target_link_libraries(tbsbbproc PUBLIC tbsdvb)

if(MSVC)
    target_compile_options(tbsbbproc PRIVATE /W4 /wd4251 /permissive-)
else()
    target_compile_options(tbsbbproc PRIVATE -Wall -Wextra)
endif()

install(TARGETS tbsbbproc EXPORT TbsDvbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/tbsbbproc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
