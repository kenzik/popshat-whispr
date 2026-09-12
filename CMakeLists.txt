cmake_minimum_required(VERSION 3.16)
project(whispr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # strlcpy/strlcat live behind __USE_MISC

find_package(Threads REQUIRED)

# Static by default. The installed binary is a single file copied to
# ~/.local/bin, and a shared build gives it a RUNPATH pointing into this build
# directory -- so deleting the clone after installing breaks whispr with a
# missing-libwhisper error that says nothing about the cause. Verified: the
# static build registers the Vulkan backend correctly and produces identical
# WER on every fixture. Override with -DBUILD_SHARED_LIBS=ON while developing.
option(BUILD_SHARED_LIBS "build shared libraries" OFF)

# Baked in at configure time for bug reports; whispr-doctor prints it.
set(WHISPR_GIT_SHA "")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE WHISPR_GIT_SHA
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
endif()

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
target_link_libraries(whispr PRIVATE whisper parakeet Threads::Threads m)

# pipe2/accept4/SOCK_CLOEXEC are GNU extensions; -std=gnu11 alone does not expose them.
target_compile_definitions(whispr PRIVATE _GNU_SOURCE)

target_compile_definitions(whispr PRIVATE
  WHISPR_VERSION="${PROJECT_VERSION}"
  WHISPR_GIT_SHA="${WHISPR_GIT_SHA}")

target_compile_options(whispr PRIVATE -Wall -Wextra -Wpedantic -Wformat-security)

target_compile_options(whispr PRIVATE
  $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection>)

install(TARGETS whispr RUNTIME DESTINATION bin)
