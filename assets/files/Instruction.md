# 摄像头配置文件
`/mnt/data/sensor_config.ini`，name 部分
```
[source]

;type = SOURCE_USER_FE

dev_num = 1

; section for sensor

[sensor]

; sensor name
name = GCORE_GC4653_MIPI_720P_60FPS_10BIT
; name = GCORE_GC4653_MIPI_4M_30FPS_10BIT

bus_id = 4

sns_i2c_addr = 29

mipi_dev = 0

lane_id = 4, 3, 2, -1, -1

pn_swap = 0, 0, 0, 0, 0

mclk_en = 1

mclk = 1

;port = 0

;pin = 2

;pol = 1

;fps = 30
```
# UART0 关闭系统消息输出
修改 `/etc/inittab` 注释第 20 行以关闭 UART0 登录。
```
# /etc/inittab
#
# This inittab is a basic inittab sample for sysvinit, which mimics
# Buildroot's default inittab for BusyBox.
id:3:initdefault:

si0::sysinit:/bin/mount -t proc proc /proc
si1::sysinit:/bin/mount -o remount,rw /
si2::sysinit:/bin/mkdir -p /dev/pts /dev/shm
si3::sysinit:/bin/mount -a
si4::sysinit:/bin/mkdir -p /run/lock/subsys
si5::sysinit:/sbin/swapon -a
si6::sysinit:/bin/ln -sf /proc/self/fd /dev/fd 2>/dev/null
si7::sysinit:/bin/ln -sf /proc/self/fd/0 /dev/stdin 2>/dev/null
si8::sysinit:/bin/ln -sf /proc/self/fd/1 /dev/stdout 2>/dev/null
si9::sysinit:/bin/ln -sf /proc/self/fd/2 /dev/stderr 2>/dev/null
si10::sysinit:/bin/hostname -F /etc/hostname
rcS:12345:wait:/etc/init.d/rcS

#-> sole::respawn:/sbin/getty -L console 0 vt100 -l /etc/console_handler.sh
acm::respawn:/sbin/getty -L ttyGS0 0 vt100 -l /etc/ttyGS0_handler.sh

# Stuff to do for the 3-finger salute
#ca::ctrlaltdel:/sbin/reboot

# Stuff to do before rebooting
shd0:06:wait:/etc/init.d/rcK
shd1:06:wait:/sbin/swapoff -a
shd2:06:wait:/bin/umount -a -r

# The usual halt or reboot actions
hlt0:0:wait:/sbin/halt -dhp
reb0:6:wait:/sbin/reboot
```

新建 `/boot/uEnv.txt`，写入以下内容：

```
consoledev=null
```

# 设置自启动

新建 `/etc/init.d/S99dart_vision`，写入以下内容：

```bash
#!/bin/sh
cd /root
MAIX_SENSOR_FPS=80 ./dart_vision model/ydd_320_320_cv181x_int8_sym.cvimodel &
```

赋予可执行权限：

```bash
chmod +x /etc/init.d/S99dart_vision
```
