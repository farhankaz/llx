cmake_minimum_required(VERSION 3.12)
project(llx)

set(LLX_VERSION "0.0.1")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static library building
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Find curl package
find_package(CURL REQUIRED)

# Add llama.cpp as a subdirectory
add_subdirectory(llama.cpp)

# Set build info variables
set(BUILD_NUMBER 0)
set(BUILD_COMMIT "local")
set(BUILD_COMPILER "unknown")
set(BUILD_TARGET "native")

# Configure build info
configure_file(
    llama.cpp/common/build-info.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/build-info.cpp
    @ONLY
)

# Create common library for llama.cpp utilities
add_library(llama_common STATIC
    llama.cpp/common/sampling.cpp
    llama.cpp/common/common.cpp
    llama.cpp/common/log.cpp
    llama.cpp/common/console.cpp
    llama.cpp/common/arg.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/build-info.cpp
)
target_include_directories(llama_common PUBLIC llama.cpp)
target_link_libraries(llama_common PUBLIC llama)

# llxd executable
add_executable(llxd
    src/llxd/main.cpp
    src/llxd/llxd.cpp
)

target_compile_definitions(llxd PRIVATE LLX_VERSION="${LLX_VERSION}")
target_link_libraries(llxd PRIVATE llama_common CURL::libcurl)
target_include_directories(llxd PRIVATE llama.cpp)

# llx executable
add_executable(llx
    src/llx/main.cpp
    src/llx/llx.cpp
    src/llx/daemon_manager.cpp
)

target_compile_definitions(llx PRIVATE LLX_VERSION="${LLX_VERSION}")
target_link_libraries(llx PRIVATE llama_common CURL::libcurl)
target_include_directories(llx PRIVATE llama.cpp) 