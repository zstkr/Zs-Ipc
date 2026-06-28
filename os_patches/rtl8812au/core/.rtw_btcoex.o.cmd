cmd_/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o := /home/ros/luckfox-pico/sysdrv/source/kernel/scripts/gcc-wrapper.py arm-rockchip830-linux-uclibcgnueabihf-gcc -Wp,-MMD,/home/ros/luckfox-pico/realtek-wlan/core/.rtw_btcoex.o.d -nostdinc -isystem /home/ros/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/../lib/gcc/arm-rockchip830-linux-uclibcgnueabihf/8.3.0/include -I./arch/arm/include -I./arch/arm/include/generated  -I./include -I./arch/arm/include/uapi -I./arch/arm/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/kconfig.h -include ./include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -fmacro-prefix-map=./= -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -std=gnu89 -fno-dwarf2-cfi-asm -fno-ipa-sra -mabi=aapcs-linux -mfpu=vfp -funwind-tables -mthumb -Wa,-mimplicit-it=always -Wa,-mno-warn-deprecated -D__LINUX_ARM_ARCH__=7 -march=armv7-a -msoft-float -Uarm -fno-delete-null-pointer-checks -Wno-frame-address -Wno-format-truncation -Wno-format-overflow -Os --param=allow-store-data-races=0 -Wframe-larger-than=1024 -fstack-protector -Werror -Wimplicit-fallthrough -Wno-unused-but-set-variable -Wno-unused-const-variable -fomit-frame-pointer -g -Wdeclaration-after-statement -Wvla -Wno-pointer-sign -Wno-stringop-truncation -Wno-array-bounds -Wno-stringop-overflow -Wno-restrict -Wno-maybe-uninitialized -fno-strict-overflow -fno-stack-check -fconserve-stack -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wno-packed-not-aligned -fno-pie -O3 -Wno-unused-variable -Wno-unused-label -Wno-unused-function -Wno-implicit-fallthrough -Wno-cast-function-type -Wno-error=incompatible-pointer-types -Wno-stringop-overread -Wno-unknown-pragmas -Wno-address -Wno-vla -g -Wno-unused-variable -Wno-unused-value -Wno-unused-label -Wno-unused-parameter -Wno-unused-function -Wno-unused -Wno-cast-function-type -Wno-date-time -Wno-uninitialized -Wno-sign-compare -Wno-type-limits -Wno-date-time -Wno-error=date-time -Wno-vla -I/home/ros/luckfox-pico/realtek-wlan/include -DCONFIG_DISABLE_REGD_C -DDBG=0 -DDRV_NAME=\"rtl88xxau\" -DCONFIG_USE_EXTERNAL_POWER -I/home/ros/luckfox-pico/realtek-wlan/hal/btc -I/home/ros/luckfox-pico/realtek-wlan/hal/phydm -DCONFIG_RTL8812A -DCONFIG_MP_INCLUDED -DCONFIG_EFUSE_CONFIG_FILE -DEFUSE_MAP_PATH=\"/system/etc/wifi/wifi_efuse_8812au.map\" -DWIFIMAC_PATH=\"/data/wifimac.txt\" -DCONFIG_TXPWR_BY_RATE_EN=0 -DCONFIG_TXPWR_LIMIT_EN=0 -DCONFIG_CALIBRATE_TX_POWER_TO_MAX -DCONFIG_RTW_ADAPTIVITY_EN=0 -DCONFIG_RTW_ADAPTIVITY_MODE=0 -DCONFIG_BR_EXT '-DCONFIG_BR_EXT_BRNAME="'br0'"' -DCONFIG_WIFI_MONITOR -DCONFIG_RTW_NAPI -DCONFIG_RTW_GRO -DCONFIG_RTW_WIFI_HAL -DCONFIG_RTW_CFGVEDNOR_LLSTATS -DCONFIG_VHT_EXTRAS -DCONFIG_LED_CONTROL -DCONFIG_LED_ENABLE -DDM_ODM_SUPPORT_TYPE=0x04 -DCONFIG_LITTLE_ENDIAN -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT  -DMODULE  -DKBUILD_BASENAME='"rtw_btcoex"' -DKBUILD_MODNAME='"88XXau"' -D__KBUILD_MODNAME=kmod_88XXau -c -o /home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o /home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.c

source_/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o := /home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.c

deps_/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o := \
    $(wildcard include/config/bt/coexist.h) \
    $(wildcard include/config/bt/coexist/socket/trx.h) \
    $(wildcard include/config/concurrent/mode.h) \
    $(wildcard include/config/rf4ce/coexist.h) \
    $(wildcard include/config/rtl8723b.h) \
  include/linux/kconfig.h \
    $(wildcard include/config/cc/version/text.h) \
    $(wildcard include/config/cpu/big/endian.h) \
    $(wildcard include/config/booger.h) \
    $(wildcard include/config/foo.h) \
  include/linux/compiler_types.h \
    $(wildcard include/config/have/arch/compiler/h.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/cc/has/asm/inline.h) \
  include/linux/compiler_attributes.h \
  include/linux/compiler-gcc.h \
    $(wildcard include/config/arm64.h) \
    $(wildcard include/config/retpoline.h) \
    $(wildcard include/config/arch/use/builtin/bswap.h) \
    $(wildcard include/config/kcov.h) \

/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o: $(deps_/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o)

$(deps_/home/ros/luckfox-pico/realtek-wlan/core/rtw_btcoex.o):
