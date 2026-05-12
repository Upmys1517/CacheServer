cmake_minimum_required(VERSION 3.10)
project(KCacheServer VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# === Paths to dependent projects ===
set(CACHESYSTEM_DIR ${CMAKE_SOURCE_DIR}/../CacheSystem)
set(MUDUO_CORE_DIR ${CMAKE_SOURCE_DIR}/../muduo-core)

# === Include directories ===
include_directories(${CACHESYSTEM_DIR})
include_directories(${CACHESYSTEM_DIR}/ArcCache)
include_directories(${MUDUO_CORE_DIR}/include)

# === muduo-core shared library (pre-built) ===
add_library(muduo_core SHARED IMPORTED)
set_target_properties(muduo_core PROPERTIES
    IMPORTED_LOCATION ${MUDUO_CORE_DIR}/lib/libmuduo_core.so
    INTERFACE_INCLUDE_DIRECTORIES ${MUDUO_CORE_DIR}/include
)

# === Server executable ===
add_executable(kcacheserver
    src/main.cc
    src/CacheServer.cc
    src/Protocol.cc
)
target_link_libraries(kcacheserver muduo_core pthread)
target_link_options(kcacheserver PRIVATE -Wl,-rpath,${MUDUO_CORE_DIR}/lib)

# === Benchmark client executable ===
add_executable(benchmark
    src/benchmark.cc
)
target_link_libraries(benchmark pthread)
