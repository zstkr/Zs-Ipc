#!/bin/bash

# 1. 检查 LUCKFOX_SDK_PATH 是否设置
if [ -z "$LUCKFOX_SDK_PATH" ]; then
    echo "=========================================================="
    echo "错误: 未检测到 LUCKFOX_SDK_PATH 环境变量！"
    echo "请在运行此脚本前，在终端运行以下命令指定 SDK 路径："
    echo "  export LUCKFOX_SDK_PATH=/home/ros/luckfox-pico"
    echo "=========================================================="
    exit 1
fi

# 2. 选择编译的库类型 (默认选择 1 - uclibc)
echo "请选择使用的 libc 类型:"
echo "1) uclibc (针对 Buildroot，默认)"
echo "2) glibc"
read -p "Enter choice [1-2] (default: 1): " choice

case $choice in
    2)
        LIBC="glibc"
        ;;
    *)
        LIBC="uclibc"
        ;;
esac

# 3. 自动配置工具链路径并指定编译器名字
echo "检测到 SDK 路径: $LUCKFOX_SDK_PATH"

if [ "$LIBC" = "uclibc" ]; then
    # 自动定位 SDK 内的 uclibc 编译器路径
    TOOLCHAIN_BIN="${LUCKFOX_SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin"
    if [ -d "$TOOLCHAIN_BIN" ]; then
        export PATH="${TOOLCHAIN_BIN}:${PATH}"
        CC_COMPILER="arm-rockchip830-linux-uclibcgnueabihf-gcc"
        CXX_COMPILER="arm-rockchip830-linux-uclibcgnueabihf-g++"
        echo ">> 自动定位：成功启用 SDK 中的 uclibc 工具链。"
    fi
else
    # 自动定位 SDK 内的 glibc 编译器路径 (部分定制版本SDK可能在此路径)
    TOOLCHAIN_BIN="${LUCKFOX_SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-gnueabihf/bin"
    if [ -d "$TOOLCHAIN_BIN" ]; then
        export PATH="${TOOLCHAIN_BIN}:${PATH}"
        CC_COMPILER="arm-rockchip830-linux-gnueabihf-gcc"
        CXX_COMPILER="arm-rockchip830-linux-gnueabihf-g++"
        echo ">> 自动定位：成功启用 SDK 中的 glibc 工具链。"
    fi
fi

# 4. 如果自动定位失败，尝试检测系统环境变量里有没有现成的
if [ -z "$CC_COMPILER" ]; then
    if [ "$LIBC" = "uclibc" ]; then
        if command -v arm-rockchip830-linux-uclibcgnueabihf-gcc &> /dev/null; then
            CC_COMPILER="arm-rockchip830-linux-uclibcgnueabihf-gcc"
            CXX_COMPILER="arm-rockchip830-linux-uclibcgnueabihf-g++"
        fi
    else
        if command -v arm-linux-gnueabihf-gcc &> /dev/null; then
            CC_COMPILER="arm-linux-gnueabihf-gcc"
            CXX_COMPILER="arm-linux-gnueabihf-g++"
        fi
    fi
fi

# 5. 如果还是找不到编译器，报错提示
if [ -z "$CC_COMPILER" ]; then
    echo "=========================================================="
    echo "错误: 未能找到交叉编译器！"
    echo "请确认您的 SDK 路径是否正确，工具链文件夹是否存在："
    echo "当前检测的路径: ${LUCKFOX_SDK_PATH}/tools/linux/toolchain/"
    echo "=========================================================="
    exit 1
fi

echo "========================================="
echo "正在使用 [ $LIBC ] 编译器进行编译..."
echo "编译器名字: $CC_COMPILER"
echo "========================================="

# 6. 创建并进入编译目录
mkdir -p build
cd build

# 清理历史编译缓存
rm -rf CMakeCache.txt

# 7. 动态传入编译器名字和 LIBC 类型给 CMake
cmake -DCMAKE_C_COMPILER=${CC_COMPILER} \
      -DCMAKE_CXX_COMPILER=${CXX_COMPILER} \
      -DLIBC_TYPE=${LIBC} ..

if [ $? -ne 0 ]; then
    echo "CMake 配置失败！"
    exit 1
fi

make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "编译失败！"
    exit 1
fi

echo "========================================="
echo "编译成功！可执行文件路径: app/build/zs_ipc_app"
echo "========================================="