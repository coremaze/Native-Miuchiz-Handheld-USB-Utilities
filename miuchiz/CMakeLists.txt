cmake_minimum_required(VERSION 3.0.0)
set(LOCAL_PROJECT_NAME miuchiz)
project(${LOCAL_PROJECT_NAME} VERSION 1.1 LANGUAGES C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMIUCHIZ_UTILS_VERSION=\"\\\"${PROJECT_VERSION}\"\\\"")

include_directories(../libmiuchiz-usb/include
                    ./include)

add_executable(${LOCAL_PROJECT_NAME} src/miuchiz.c
                                     src/actions/dump-flash.c
                                     src/actions/eject.c
                                     src/actions/load-flash.c
                                     src/actions/read-creditz.c
                                     src/actions/set-creditz.c
                                     src/actions/status.c)

set_property(TARGET ${LOCAL_PROJECT_NAME} PROPERTY C_STANDARD 11)

if (WIN32)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/windows-assistance/console.bat
         DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
ENDIF()

target_link_libraries(${LOCAL_PROJECT_NAME} miuchiz-usb) 
INSTALL(TARGETS ${LOCAL_PROJECT_NAME} DESTINATION bin)
INSTALL(FILES completions/miuchiz DESTINATION share/bash-completion/completions)