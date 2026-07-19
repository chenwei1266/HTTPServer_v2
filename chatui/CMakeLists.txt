cmake_minimum_required(VERSION 3.10)
project(ChatUI_v2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(OpenSSL REQUIRED)
find_library(MUDUO_NET muduo_net)
find_library(MUDUO_BASE muduo_base)
find_library(MYSQL_CONNECTOR mysqlcppconn)
find_library(CURL_LIB curl)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../HttpServer_v2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${OPENSSL_INCLUDE_DIR}
)

# HttpServer_v2 源文件
file(GLOB_RECURSE HTTPSERVER_SOURCES
    ../HttpServer_v2/src/*.cpp
)

# ChatUI 源文件
set(CHATUI_SOURCES
    main.cpp
    application/ChatUI.cpp
    llm/LLMManager.cpp
    user/UserManager.cpp
    storage/StorageManager.cpp
    storage/UserRepository.cpp
    storage/ConversationRepository.cpp
    storage/MessageRepository.cpp
)

# 生成可执行文件
add_executable(chatui_v2_server
    ${CHATUI_SOURCES}
    ${HTTPSERVER_SOURCES}
)

# 链接库
target_link_libraries(chatui_v2_server
    ${MUDUO_NET}
    ${MUDUO_BASE}
    ${MYSQL_CONNECTOR}
    ${CURL_LIB}
    ${OPENSSL_LIBRARIES}
    redis++
    hiredis
    pthread
)

# 输出信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
