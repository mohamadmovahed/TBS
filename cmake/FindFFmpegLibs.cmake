# Locates libavformat, libavcodec, libavutil and libswscale and provides tbs::ffmpeg.
# Tries pkg-config first, then a plain header/library search (set FFMPEG_ROOT to help it,
# e.g. on Windows with the shared "ffmpeg-*-full_build-shared" packages).
if(TARGET tbs::ffmpeg)
    return()
endif()

set(_tbs_ff_components avformat avcodec swscale avutil)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(TBS_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
    if(TBS_FFMPEG_FOUND)
        add_library(tbs::ffmpeg INTERFACE IMPORTED)
        target_link_libraries(tbs::ffmpeg INTERFACE PkgConfig::TBS_FFMPEG)
        message(STATUS "Found FFmpeg (pkg-config)")
        return()
    endif()
endif()

find_path(TBS_FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS ${FFMPEG_ROOT} ENV FFMPEG_ROOT PATH_SUFFIXES include)
set(_tbs_ff_libs)
foreach(_c ${_tbs_ff_components})
    find_library(TBS_FFMPEG_${_c}_LIBRARY NAMES ${_c}
        HINTS ${FFMPEG_ROOT} ENV FFMPEG_ROOT PATH_SUFFIXES lib)
    if(NOT TBS_FFMPEG_${_c}_LIBRARY)
        return()
    endif()
    list(APPEND _tbs_ff_libs ${TBS_FFMPEG_${_c}_LIBRARY})
endforeach()
if(TBS_FFMPEG_INCLUDE_DIR)
    add_library(tbs::ffmpeg INTERFACE IMPORTED)
    target_include_directories(tbs::ffmpeg INTERFACE ${TBS_FFMPEG_INCLUDE_DIR})
    target_link_libraries(tbs::ffmpeg INTERFACE ${_tbs_ff_libs})
    message(STATUS "Found FFmpeg: ${TBS_FFMPEG_INCLUDE_DIR}")
endif()
