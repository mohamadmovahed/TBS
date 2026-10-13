# Qt GUI for the tbsdvb library
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
set(CMAKE_AUTOMOC ON)
message(STATUS "GUI: Qt ${QT_VERSION}")

add_executable(tbs-receiver
    main.cpp
    MainWindow.cpp MainWindow.h
    ReceiverController.cpp ReceiverController.h
    SettingsStore.cpp SettingsStore.h
    Theme.cpp Theme.h
    panels/FormHelpers.h
    panels/TuningPanel.cpp panels/TuningPanel.h
    panels/OutputPanel.cpp panels/OutputPanel.h
    panels/NetOutputDialog.cpp panels/NetOutputDialog.h
    panels/OverviewPanel.cpp panels/OverviewPanel.h
    panels/PidTable.cpp panels/PidTable.h
    panels/ServicesPanel.cpp panels/ServicesPanel.h
    panels/IpPanel.cpp panels/IpPanel.h
    panels/EpgPanel.cpp panels/EpgPanel.h
    panels/MonitorPanel.cpp panels/MonitorPanel.h
    widgets/HistoryChart.cpp widgets/HistoryChart.h
    widgets/ShareBarDelegate.cpp widgets/ShareBarDelegate.h
    widgets/MetricTile.cpp widgets/MetricTile.h
    widgets/StatusPill.cpp widgets/StatusPill.h
    widgets/LockChain.cpp widgets/LockChain.h
    widgets/ToggleSwitch.cpp widgets/ToggleSwitch.h
    video/VideoWidget.cpp video/VideoWidget.h
)
target_include_directories(tbs-receiver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tbs-receiver PRIVATE TbsDvb::tbsdvb TbsDvb::tbsbbproc
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(tbs-receiver PRIVATE TBS_APP_VERSION="${PROJECT_VERSION}")

if(TBS_ENABLE_FFMPEG)
    include(FindFFmpegLibs)
    if(TARGET tbs::ffmpeg)
        target_sources(tbs-receiver PRIVATE video/VideoDecoder.cpp video/VideoDecoder.h video/AvSync.cpp video/AvSync.h)
        target_link_libraries(tbs-receiver PRIVATE tbs::ffmpeg)
        target_compile_definitions(tbs-receiver PRIVATE TBS_HAVE_FFMPEG=1)
        message(STATUS "GUI video preview: ON")
        # Audio: decoded with libswresample, played with Qt Multimedia (both optional)
        include(FindSwrLib)
        find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Multimedia)
        if(TARGET tbs::swresample AND TARGET Qt${QT_VERSION_MAJOR}::Multimedia AND QT_VERSION_MAJOR EQUAL 6)
            target_sources(tbs-receiver PRIVATE video/AudioOutput.cpp video/AudioOutput.h)
            target_link_libraries(tbs-receiver PRIVATE tbs::swresample Qt${QT_VERSION_MAJOR}::Multimedia)
            target_compile_definitions(tbs-receiver PRIVATE TBS_HAVE_AUDIO=1)
            message(STATUS "GUI audio preview: ON")
        else()
            message(STATUS "GUI audio preview: OFF (needs Qt 6 Multimedia and libswresample)")
        endif()
    else()
        message(WARNING "FFmpeg not found - video preview disabled")
    endif()
endif()

if(MSVC)
    target_compile_options(tbs-receiver PRIVATE /W4 /wd4251)
else()
    target_compile_options(tbs-receiver PRIVATE -Wall -Wextra)
endif()

set_target_properties(tbs-receiver PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
install(TARGETS tbs-receiver RUNTIME DESTINATION bin BUNDLE DESTINATION .)
