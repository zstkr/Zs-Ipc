#!/bin/bash

# ==============================================================================
# 请将下面的路径修改为您本地 Luckfox SDK 的实际绝对路径
# ==============================================================================
LUCKFOX_SDK_PATH="/your/local/path/to/luckfox-pico"


# 自动拼接交叉编译器路径和内核源码路径
CROSS_COMPILE_PREFIX="${LUCKFOX_SDK_PATH}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-"
KERNEL_SRC="${LUCKFOX_SDK_PATH}/sysdrv/source/kernel"

echo "========================================="
echo "开始编译 RTL8812AU 无线网卡驱动..."
echo "SDK 路径: ${LUCKFOX_SDK_PATH}"
echo "========================================="

# 执行交叉编译命令
make ARCH=arm \
  CROSS_COMPILE="${CROSS_COMPILE_PREFIX}" \
  KSRC="${KERNEL_SRC}" \
  CONFIG_PLATFORM_I386_PC=n \
  CONFIG_PLATFORM_ARM_RPI=y

if [ $? -eq 0 ]; then
    echo "========================================="
    echo "🎉 编译成功！请在当前目录下找到 8812au.ko 文件。"
    echo "========================================="
else
    echo "========================================="
    echo "❌ 编译失败，请检查 LUCKFOX_SDK_PATH 路径是否正确。"
    echo "========================================="
    exit 1
fi