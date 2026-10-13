# Installers with CPack:
#   Linux:   DEB (dependencies from dpkg-shlibdeps) and TGZ   ->  cpack -G DEB
#   Windows: NSIS installer and ZIP                           ->  cpack -G NSIS
#   macOS:   DMG with TBS Receiver.app                         ->  cpack -G DragNDrop
# The AppImage is built by packaging/appimage/build-appimage.sh.
set(CPACK_PACKAGE_NAME "tbs-receiver")
set(CPACK_PACKAGE_VENDOR "TBS Receiver project")
set(CPACK_PACKAGE_CONTACT "TBS Receiver maintainers <tbs-receiver@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "TBS 6903x DVB-S2/S2X receiver: capture, streaming, analysis and monitoring")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "TBS Receiver")
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/README.md)
set(CPACK_PACKAGE_EXECUTABLES "tbs-receiver;TBS Receiver")
set(CPACK_STRIP_FILES ON)
set(CPACK_SOURCE_IGNORE_FILES "/build.*/;/\\\\.git/")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "python3")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
        "Tunes TBS 6903x (STiD135) cards, captures TS or BBFrames, writes files, publishes over ZeroMQ,\n streams TS or single services over UDP/RTP/SRT, analyses PSI/SI, EPG, GSE and MPE, and monitors\n the receiver with alarms. Includes the Qt GUI, the tbs-daemon REST/WebSocket service, command\n line tools, the libraries with headers, and the Python module.")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mohamadmovahed/TBS")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "TBS Receiver ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "TBS Receiver")
    set(CPACK_NSIS_MUI_ICON ${PROJECT_SOURCE_DIR}/packaging/windows/tbs-receiver.ico)
    set(CPACK_NSIS_MUI_UNIICON ${PROJECT_SOURCE_DIR}/packaging/windows/tbs-receiver.ico)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\tbs-receiver.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH ON) # offers to put bin\\ on PATH for tbs-cli / tbs-daemon
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\TBS Receiver.lnk' '$INSTDIR\\\\bin\\\\tbs-receiver.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$SMPROGRAMS\\\\$START_MENU\\\\TBS Receiver.lnk'")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "TBS Receiver")
endif()

include(CPack)
