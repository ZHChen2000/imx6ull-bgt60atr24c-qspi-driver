[English](README.md)

# Infineon BGT60ATR24C QSPI驱动扩展

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

需要打开（或在 menuconfig 里选）：

```
CONFIG_SPI_FSL_QUADSPI=y
CONFIG_BGT60ATR24C=m
```

仓库里有一份配置片段 `kernel/configs/bgt60_defconfig.fragment`（含 MTD/QSPI 依赖项）。在内核源码目录里合并到现有 `.config` 例如：

```bash
cd /path/to/linux-4.1.15
./scripts/kconfig/merge_config.sh -m .config /path/to/imx6ull-bgt60atr24c-qspi-driver/kernel/configs/bgt60_defconfig.fragment
make olddefconfig
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

在仓库根目录执行 `cd tools && make` 即可编译；Makefile 默认用本仓库的 `kernel/include/uapi` 里的头文件。若已在板子上装好内核头，可指定：

```bash
cd tools
make KERNEL_SRC=/path/to/linux-4.1.15
./bgt60_capture -n 10
```

## 许可证

GPL-2.0

## 联系方式

E-mail : zhchen2000@foxmail.com
