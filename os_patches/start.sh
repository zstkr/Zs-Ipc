#!bin/sh
set -e

WLAN_IF="wlan0"
WLAN_IP="192.168.50.1"
NETMASK="255.255.255.0"
HOSTAPD_CONF="/etc/hostapd.conf"

CFG80211_KO="./cfg80211.ko"
RTL88XXAU_KO="./88XXau.ko"
MPI_SCRIPT="./insmod_mpi.sh"

echo "[1/6] 加载 cfg80211.ko..."
if [ -f "$CFG80211_KO" ]; then
    insmod "$CFG80211_KO" || true
fi

echo "[2/6] 加载 88XXau.ko (带防休眠参数)..."
if [ -f "$RTL88XXAU_KO" ]; then
    insmod "$RTL88XXAU_KO" rtw_power_mgnt=0 rtw_enusbss=0 rtw_ips_mode=0 || true
fi

echo "[3/6] 执行 insmod_mpi.sh..."
if [ -f "$MPI_SCRIPT" ]; then
    chmod +x "$MPI_SCRIPT"
    "$MPI_SCRIPT"
fi

echo "[4/6] 启动网卡 $WLAN_IF..."
ifconfig "$WLAN_IF" up

echo "[5/6] 配置 IP 地址..."
ifconfig "$WLAN_IF" "$WLAN_IP" netmask "$NETMASK"

echo "[6/6] 启动 hostapd..."
if [ -f "$HOSTAPD_CONF" ]; then
    hostapd -B "$HOSTAPD_CONF"
fi

echo "[7/7] 强制锁定 USB 电源状态为永不休眠..."       =============
echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null || true
for f in /sys/bus/usb/devices/*/power/control; do
    if [ -w "$f" ]; then
        echo on > "$f" 2>/dev/null || true                  =======
    fi
done
for f in /sys/bus/usb/devices/*/power/autosuspend; do       =======
    if [ -w "$f" ]; then
        echo -1 > "$f" 2>/dev/null || true
    fi                                                      =======
done

echo "WiFi AP 启动完成！USB 休眠已被物理级锁定。"           =======
