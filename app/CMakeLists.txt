cmake_minimum_required(VERSION 3.12)
project(zs_ipc_app)

# ==========================================================
# 1. 编译器设置 (默认使用 uclibc 交叉编译器)
# ==========================================================
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
    set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ==========================================================
# 2. 头文件搜索路径 (完全对齐你当前的真实目录)
# ==========================================================
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/rkaiq
    ${PROJECT_SOURCE_DIR}/include/rkaiq/uAPI2
    ${PROJECT_SOURCE_DIR}/include/rkaiq/common         # 解决 rk_aiq.h 找不到的问题
    ${PROJECT_SOURCE_DIR}/include/rkaiq/xcore          # 解决核心数据流头文件
    ${PROJECT_SOURCE_DIR}/include/rkaiq/algos          # 解决ISP/3A算法头文件
    ${PROJECT_SOURCE_DIR}/include/rkaiq/iq_parser      # 解决 RkAiqCalibDbTypes.h 找不到的问题
    ${PROJECT_SOURCE_DIR}/include/rkaiq/iq_parser_v2   # 解决 V2 标定解析接口
    ${PROJECT_SOURCE_DIR}/include/librga              # 解决 RgaUtils.h 找不到的问题
    ${PROJECT_SOURCE_DIR}/ipc_app/include             # 你的核心业务头文件
    ${PROJECT_SOURCE_DIR}/3rdparty/allocator/dma
    ${PROJECT_SOURCE_DIR}/3rdparty/allocator/drm
)

# ==========================================================
# 3. 动态库搜索路径
# ==========================================================
set(LIBC_TYPE "uclibc" CACHE STRING "Target libc type (uclibc or glibc)")
link_directories(${PROJECT_SOURCE_DIR}/lib/${LIBC_TYPE})

# ==========================================================
# 4. 收集源文件 (包含 .cc 和 .cpp 后缀)
# ==========================================================
file(GLOB_RECURSE SRC_FILES 
    "ipc_app/src/*.cc" 
    "ipc_app/src/*.cpp" 
    "ipc_app/src/*.c"
    "3rdparty/allocator/dma/*.cpp"
    "3rdparty/allocator/drm/*.cpp"
)

# ==========================================================
# 5. 生成可执行程序
# ==========================================================
add_executable(${PROJECT_NAME} ${SRC_FILES})

# ==========================================================
# 6. 链接瑞芯微的多媒体库与线程库 (解决动态库间接依赖问题)
# ==========================================================
target_link_libraries(${PROJECT_NAME}
    rockit
    rockchip_mpp
    rkaiq
    rga                  # 1. 显式链接 rga 动态库，解决 undefined reference 问题
    pthread
    #2. 显式指定间接依赖动态库的搜索路径，彻底解决 librga.so needed by librockit.so not found 的警告
    "-Wl,-rpath-link,${PROJECT_SOURCE_DIR}/lib/${LIBC_TYPE}"
)