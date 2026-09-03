cmake_minimum_required(VERSION 3.16)
project(zathas_ai VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# ── llama.cpp ──────────────────────────────────────────────────────────────────
FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG        b5615   # pin to a stable build
  GIT_SHALLOW    TRUE
)
set(LLAMA_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER    OFF CACHE BOOL "" FORCE)

# Building llama-finetune lets you produce LoRA adapters to use with --lora.
# It pulls in all of llama.cpp's examples, so it is off by default.
option(ZATHAS_BUILD_FINETUNE "Build the llama-finetune binary for LoRA training" OFF)
if(ZATHAS_BUILD_FINETUNE)
  set(LLAMA_BUILD_COMMON   ON  CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES ON  CACHE BOOL "" FORCE)
else()
  set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(llama_cpp)

# ── OpenSSL (for cloud inference HTTPS calls) ─────────────────────────────────
# Install via MSYS2: pacman -S mingw-w64-ucrt-x86_64-openssl
find_package(OpenSSL REQUIRED)

# ── cpp-httplib ────────────────────────────────────────────────────────────────
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.5
  GIT_SHALLOW    TRUE
)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE ON  CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ── nlohmann/json ──────────────────────────────────────────────────────────────
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── zlib (for PDF FlateDecode decompression) ──────────────────────────────────
# Install via MSYS2: pacman -S mingw-w64-ucrt-x86_64-zlib
find_package(ZLIB REQUIRED)

# ── Optional server modules ───────────────────────────────────────────────────
# A deployment may drop .cpp files into src/modules/impl/ to mount extra routes
# (see src/modules/module.h). When that directory contains sources they are
# built in place of the default no-op, so a plain checkout always builds and
# runs cleanly with nothing extra to install.
file(GLOB ZATHAS_MODULE_IMPL CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/impl/*.cpp)
if(ZATHAS_MODULE_IMPL)
  set(ZATHAS_MODULE_SOURCES ${ZATHAS_MODULE_IMPL})
  message(STATUS "Server modules: implementation found in src/modules/impl")
else()
  set(ZATHAS_MODULE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/none.cpp)
  message(STATUS "Server modules: none (default build)")
endif()

# ── Main executable ────────────────────────────────────────────────────────────
add_executable(zathas_ai
  src/main.cpp
  src/inference.cpp
  src/cloud_inference.cpp
  src/server.cpp
  src/pdf_extract.cpp
  src/pdf_render.cpp
  src/office_extract.cpp
  ${ZATHAS_MODULE_SOURCES}
)

target_include_directories(zathas_ai PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(zathas_ai PRIVATE
  llama
  httplib::httplib
  nlohmann_json::nlohmann_json
  ZLIB::ZLIB
)

# A module implementation may declare extra dependencies of its own by shipping
# src/modules/impl/modules.cmake — included here, after the target exists, so it
# can call find_package()/target_link_libraries() as needed.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/impl/modules.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/src/modules/impl/modules.cmake)
endif()

# Copy frontend files next to the binary after build
add_custom_command(TARGET zathas_ai POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/frontend
    $<TARGET_FILE_DIR:zathas_ai>/frontend
  COMMENT "Copying frontend files..."
)

# ── Platform tweaks ────────────────────────────────────────────────────────────
if(WIN32)
  target_link_libraries(zathas_ai PRIVATE ws2_32)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(zathas_ai PRIVATE pthread dl)
endif()
