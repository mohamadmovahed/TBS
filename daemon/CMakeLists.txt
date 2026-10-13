# tbs-daemon: headless receiver with a REST/WebSocket API and a web dashboard
set(TBS_DASHBOARD_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/Dashboard.h)
add_custom_command(
    OUTPUT ${TBS_DASHBOARD_HEADER}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html -DOUTPUT=${TBS_DASHBOARD_HEADER}
            -DNAME=kDashboardHtml -P ${CMAKE_CURRENT_SOURCE_DIR}/EmbedFile.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html ${CMAKE_CURRENT_SOURCE_DIR}/EmbedFile.cmake
    COMMENT "Embedding the daemon dashboard")

add_library(tbs-daemon-core STATIC
    Json.cpp Json.h
    HttpServer.cpp HttpServer.h
    DaemonConfig.cpp DaemonConfig.h
    Daemon.cpp Daemon.h
    ScanApi.cpp
    ${TBS_DASHBOARD_HEADER})
target_include_directories(tbs-daemon-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(tbs-daemon-core PUBLIC TbsDvb::tbsbbproc)
if(WIN32)
    target_link_libraries(tbs-daemon-core PUBLIC ws2_32)
endif()
if(NOT MSVC)
    target_compile_options(tbs-daemon-core PRIVATE -Wall -Wextra)
endif()

add_executable(tbs-daemon main.cpp)
target_link_libraries(tbs-daemon PRIVATE tbs-daemon-core)
set_target_properties(tbs-daemon PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
install(TARGETS tbs-daemon RUNTIME DESTINATION bin)
install(FILES ${PROJECT_SOURCE_DIR}/packaging/tbs-daemon.json DESTINATION share/tbs-receiver)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/tbs-daemon.service DESTINATION share/tbs-receiver)
endif()
