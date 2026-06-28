## 📁 os_patches 目录说明

该目录包含专为定制主板设计的板级配置、裁剪后的内核及文件系统配置补丁：

*   `BoardConfig-SPI_NAND-Buildroot-RV1106-MyBoard-IPC.mk`：定制板级配置文件
*   `custom_kernel_defconfig`：精简了显卡、触摸屏、键盘等驱动后的轻量级内核配置
*   `custom_buildroot_defconfig`：精简了 Python，将 OpenSSH 替换为轻量 Dropbear 后的系统配置
*   `rv1106-zs-ipc.dts`：定制硬件外设（网卡引脚等）的设备树文件
*   `start.sh`：主板一键启动脚本（加载驱动、配置热点、防休眠等）
*   `rtl8812au/`：无线网卡驱动源码与交叉编译说明

---

## 🛠️ 如何在您的 Luckfox SDK 中应用这些配置？

克隆本项目后，请按照以下步骤将 `os_patches` 目录下的配置文件覆盖到您本地官方标准的 Luckfox SDK 中：

### 1. 拷贝并应用配置文件

请在您的开发环境中执行以下覆盖拷贝操作（注意将 `<Luckfox_SDK_Path>` 替换为您本地 SDK 的实际路径）：

# 拷贝定制板级配置
cp os_patches/BoardConfig-SPI_NAND-Buildroot-RV1106-ZS-IPC.mk <Luckfox_SDK_Path>/project/cfg/BoardConfig_IPC/

# 拷贝裁剪内核配置
cp os_patches/custom_kernel_defconfig <Luckfox_SDK_Path>/sysdrv/source/kernel/arch/arm/configs/luckfox_rv1106_linux_defconfig

# 拷贝精简文件系统配置
cp os_patches/custom_buildroot_defconfig <Luckfox_SDK_Path>/sysdrv/source/buildroot/buildroot-2023.02.6/configs/luckfox_pico_defconfig

# 拷贝定制设备树
cp os_patches/rv1106-zs-ipc.dts <Luckfox_SDK_Path>/sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts

### 2. 激活并载入定制板子
进入您的 <Luckfox_SDK_Path> 根目录下，运行 ./build.sh 选择板子型号：
弹出第一级菜单时，输入 11 选择 custom。
在随后弹出的长列表中，找到我们的定制配置文件：
BoardConfig-SPI_NAND-Buildroot-RV1106-ZS-IPC.mk
输入它前面对应的 序号，回车确认，完成激活。

### 3. 一键编译系统与打包固件
在 SDK 根目录下依次执行：

./build.sh clean
./build.sh all
./build.sh firmware

### 4.运行一键启动脚本 start.sh
编译好的固件默认包含了我们编写的 start.sh 脚本，它负责：
自动加载无线网卡驱动（insmod 8812au.ko）
开启局域网 Wi-Fi AP 热点
物理锁定 USB 接口防休眠，确保图传链路不中断
1. 手动运行测试
将 start.sh 拷贝到板子中，赋予权限并执行：
code
Bash
chmod +x start.sh
./start.sh
2. 配置开机自动运行 (Buildroot 自启规范)
如果您希望板子一上电就自动开启热点和驱动，请在编译前或在板子终端内进行以下配置：
将 start.sh 放置到系统的 /etc/init.d/ 目录下。
重命名为 S99zs_ipc（Buildroot 会按字母顺序在开机最后执行 S99 开头的脚本）：
code
Bash
mv /path/to/start.sh /etc/init.d/S99zs_ipc
chmod +x /etc/init.d/S99zs_ipc## 💾 免编译快速烧录体验 (Pre-compiled Firmware)

如果您不想配置复杂的交叉编译环境，只想快速体验本项目，我们为您提供了已经编译、裁剪、配置完毕的一键烧录固件及 Android App 安装包：

👉 **[点击前往 GitHub Releases 页面下载最新固件与 APK](https://github.com/您的用户名/zs-ipc/releases)**

下载后：
1. 开发板端：直接使用官方线刷烧录工具，将 `update.img` 烧录至您的定制主板（SPI NAND 启动）。
2. 手机端：直接在安卓手机上安装解压出的 `.apk` 文件。
