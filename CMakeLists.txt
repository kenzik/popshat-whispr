cmake_minimum_required(VERSION 3.16)
project(whispr C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # strlcpy/strlcat live behind __USE_MISC

find_package(Threads REQUIRED)

set(WHISPER_BUILD_EXAMPLES ON  CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/whisper.cpp EXCLUDE_FROM_ALL)

add_executable(whispr
  src/main.c
  src/daemon.c
  src/engine.c
  src/audio.c
  src/ipc.c
  src/config.c)

target_include_directories(whispr PRIVATE src)
target_link_libraries(whispr PRIVATE whisper Threads::Threads)

# pipe2/accept4/SOCK_CLOEXEC are GNU extensions; -std=gnu11 alone does not expose them.
target_compile_definitions(whispr PRIVATE _GNU_SOURCE)

target_compile_options(whispr PRIVATE -Wall -Wextra -Wpedantic -Wformat-security)

target_compile_options(whispr PRIVATE
  $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection>)

install(TARGETS whispr RUNTIME DESTINATION bin)
