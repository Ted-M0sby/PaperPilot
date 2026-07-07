cmake_minimum_required(VERSION 3.20)
project(paperpilot_c VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(paperpilot_core
  src/cli.c
  src/config.c
  src/document.c
  src/index.c
  src/query.c
  src/logging.c
  src/stats.c
  src/ai_client.c
  src/server.c
)

target_include_directories(paperpilot_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(paperpilot src/main.c)
target_link_libraries(paperpilot PRIVATE paperpilot_core)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_basic.c")
  enable_testing()
  add_executable(test_basic tests/test_basic.c)
  target_link_libraries(test_basic PRIVATE paperpilot_core)
  add_test(NAME basic COMMAND test_basic)
endif()

if(WIN32)
  target_link_libraries(paperpilot PRIVATE ws2_32)
  if(TARGET test_basic)
    target_link_libraries(test_basic PRIVATE ws2_32)
  endif()
endif()
