cmake_minimum_required(VERSION 3.10)
project(KCacheServer VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# === Paths to dependent projects ===
set(CACHESYSTEM_DIR ${CMAKE_SOURCE_DIR}/../CacheSystem)
set(MUDUO_CORE_DIR ${CMAKE_SOURCE_DIR}/../muduo_core)

# === Include directories ===
include_directories(${CACHESYSTEM_DIR})
include_directories(${CACHESYSTEM_DIR}/ArcCache)
include_directories(${MUDUO_CORE_DIR}/include)

# === muduo_core library ===
# Build muduo_core first: cd ../muduo_core && mkdir -p build && cd build && cmake .. && make
set(MUDUO_CORE_LIB ${MUDUO_CORE_DIR}/build/src/libmuduo_core.so)
if(NOT EXISTS ${MUDUO_CORE_LIB})
    message(FATAL_ERROR
        "muduo_core library not found at: ${MUDUO_CORE_LIB}\n"
        "Please build it first:\n"
        "  cd ${MUDUO_CORE_DIR}\n"
        "  mkdir -p build && cd build\n"
        "  cmake .. && make -j$$(nproc)")
endif()

add_library(muduo_core SHARED IMPORTED)
set_target_properties(muduo_core PROPERTIES
    IMPORTED_LOCATION ${MUDUO_CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${MUDUO_CORE_DIR}/include
)

# === Server executable ===
add_executable(kcacheserver
    src/main.cc
    src/CacheServer.cc
    src/Protocol.cc
)
target_link_libraries(kcacheserver muduo_core pthread)

# === Benchmark client executable ===
# Standalone, does not depend on muduo_core or CacheSystem
add_executable(benchmark
    src/benchmark.cc
)
target_link_libraries(benchmark pthread)
