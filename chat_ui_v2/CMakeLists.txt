cmake_minimum_required(VERSION 3.10)
project(ChatUIV2Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

set(HTTP_SERVER_ROOT "${CMAKE_SOURCE_DIR}/../HttpServer_v2")

include_directories(
    ${HTTP_SERVER_ROOT}/include
    ${CMAKE_SOURCE_DIR}
)

file(GLOB_RECURSE HTTP_SERVER_SRCS
    "${HTTP_SERVER_ROOT}/src/*.cpp"
)

add_executable(chatui_v2_server
    chat_main.cpp
    ${HTTP_SERVER_SRCS}
)

target_link_libraries(chatui_v2_server
    muduo_net
    muduo_base
    pthread
    ssl
    crypto
    mysqlcppconn
    CURL::libcurl
)

add_custom_command(TARGET chatui_v2_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/chatui.html
        $<TARGET_FILE_DIR:chatui_v2_server>/chatui.html
)
