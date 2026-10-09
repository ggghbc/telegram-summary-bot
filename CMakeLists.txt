cmake_minimum_required(VERSION 3.20)
project(telegram_summary_bot VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimization options
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -pipe)
    endif()
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# Include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/config.cpp
    src/db.cpp
    src/http_client.cpp
    src/telegram_bot.cpp
    src/llm_client.cpp
    src/summary_generator.cpp
    src/bot_app.cpp
    src/main.cpp
)

# Executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    CURL::libcurl
    SQLite3::SQLite3
)

# Target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "telegram-summary-bot"
)

# Test suite executable
add_executable(test_all
    tests/test_all.cpp
    src/db.cpp
    src/telegram_bot.cpp
    src/http_client.cpp
    src/llm_client.cpp
    src/summary_generator.cpp
    src/config.cpp
)

target_link_libraries(test_all PRIVATE
    Threads::Threads
    CURL::libcurl
    SQLite3::SQLite3
)
