[English](README.md)

# Infineon BGT60ATR24C QSPI驱动扩展（NXP i.MX6ULL Linux 4.1.x）

![i.MX6ULL Quad SPI 连接 BGT60ATR24C](ref.png)

## 目录

```
kernel/          驱动源码
patches/         0001–0003补丁序列
scripts/         apply-to-kernel.sh / generate-patches.py
tools/           用户态数据采集程序
```

## 要求

- Linux 内核 : 4.1.x
- SoC : NXP i.MX6ULL

## 集成

**方式 A — 通过Scripts安装：**

```bash
./scripts/apply-to-kernel.sh /path/to/linux-4.1.15
```

**方式 B — 通过Patches合并：**

```bash
cd /path/to/linux-4.1.15
patch -p1 < patches/0001-qspi-add-ip-op.patch
patch -p1 < patches/0002-bgt60atr24c-driver.patch
patch -p1 < patches/0003-dts-enable-bgt60.patch
```

## 内核配置

```
CONFIG_SPI_FSL_QUADSPI=y
CONFIG_BGT60ATR24C=m
```

## 设备树

```dts
#include "imx6ull-bgt60atr24c-qspi.dtsi"
```

属性说明见`infineon,bgt60atr24c.txt`

## 编译

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

## 用户接口

| 接口 | 说明 |
|------|------|
| /dev/bgt60atr24c | read / poll / ioctl |
| sysfs | state、counters |
| uapi | kernel/include/uapi/linux/bgt60atr24c.h |
| ioctl | START/STOP/RECOVER/GET/SET_CFG/GET_COUNTERS/GET_INFO |

## 可选工具

```bash
cd tools && make
./bgt60_capture -n 10
```

## 许可证

GPL-2.0

## 联系方式

E-mail : zhchen2000@foxmail.com
