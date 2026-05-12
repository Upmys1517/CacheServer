# 指定 CMake 的最低版本要求，确保项目可以正常配置
cmake_minimum_required(VERSION 3.10)

# 定义项目名称、版本号和使用的编程语言
project(KCacheServer VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准为 C++17，启用 C++17 特性支持
set(CMAKE_CXX_STANDARD 17)

# 要求编译器必须支持 C++17 标准，如果不支持则报错终止配置
set(CMAKE_CXX_STANDARD_REQUIRED True)

# === 依赖项目的路径配置 ===

# 设置 CacheSystem 库的根目录路径（相对于当前 CMakeLists.txt 的上层目录）
set(CACHESYSTEM_DIR ${CMAKE_SOURCE_DIR}/../CacheSystem)

# 设置 muduo-core 网络库的根目录路径（相对于当前 CMakeLists.txt 的上层目录）
set(MUDUO_CORE_DIR ${CMAKE_SOURCE_DIR}/../muduo-core)

# === 头文件包含目录配置 ===

# 将 CacheSystem 根目录加入头文件搜索路径
include_directories(${CACHESYSTEM_DIR})

# 将 ArcCache 子目录加入头文件搜索路径（支持 ARC 缓存策略）
include_directories(${CACHESYSTEM_DIR}/ArcCache)

# 将 muduo-core 的 include 目录加入头文件搜索路径
include_directories(${MUDUO_CORE_DIR}/include)

# === muduo-core 预编译共享库 ===

# 声明一个导入的(IMPORTED)共享库目标，表示该库是外部预编译的
add_library(muduo_core SHARED IMPORTED)

# 设置 muduo_core 库的属性：库文件位置和头文件包含目录
set_target_properties(muduo_core PROPERTIES
    # 指定预编译的 .so 文件在磁盘上的绝对路径
    IMPORTED_LOCATION ${MUDUO_CORE_DIR}/lib/libmuduo_core.so
    # 指定使用该库时需要的头文件目录
    INTERFACE_INCLUDE_DIRECTORIES ${MUDUO_CORE_DIR}/include
)

# === 服务器可执行文件 ===

# 定义要构建的可执行文件 kcacheserver，指定其源文件列表
add_executable(kcacheserver
    src/main.cc          # 程序入口，解析命令行参数并启动服务器
    src/CacheServer.cc   # 缓存服务器核心逻辑，处理连接和命令
    src/Protocol.cc      # 协议解析器，负责请求解析和响应编码
)

# 链接依赖库：muduo_core（网络框架）和 pthread（POSIX 线程库）
target_link_libraries(kcacheserver muduo_core pthread)

# 设置链接选项：将 muduo_core 库的路径写入可执行文件的 RPATH，
# 使得运行时能在指定目录下找到 libmuduo_core.so，无需设置 LD_LIBRARY_PATH
target_link_options(kcacheserver PRIVATE -Wl,-rpath,${MUDUO_CORE_DIR}/lib)

# === 基准测试客户端可执行文件 ===

# 定义要构建的基准测试可执行文件 benchmark
add_executable(benchmark
    src/benchmark.cc     # 基准测试客户端，用于压力测试和性能评估
)

# 链接 pthread 库（由于使用了多线程进行并发连接测试）
target_link_libraries(benchmark pthread)
