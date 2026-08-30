[中文](README.zh.md)

# Infineon BGT60ATR24C QSPI driver extension

![i.MX6ULL Quad SPI connection to BGT60ATR24C](ref.png)

## Layout

```
kernel/          driver sources
patches/         patch series 0001–0003
scripts/         apply-to-kernel.sh / generate-patches.py
tools/           userspace data capture tool
```

## Requirements

- Linux kernel: 4.1.x
- SoC: NXP i.MX6ULL

## Integration

**Option A — install via scripts:**

```bash
./scripts/apply-to-kernel.sh /path/to/linux-4.1.15
```

**Option B — merge via patches:**

```bash
cd /path/to/linux-4.1.15
patch -p1 < patches/0001-qspi-add-ip-op.patch
patch -p1 < patches/0002-bgt60atr24c-driver.patch
patch -p1 < patches/0003-dts-enable-bgt60.patch
```

## Kernel configuration

```
CONFIG_SPI_FSL_QUADSPI=y
CONFIG_BGT60ATR24C=m
```

## Device tree

```dts
#include "imx6ull-bgt60atr24c-qspi.dtsi"
```

See `infineon,bgt60atr24c.txt` for property descriptions.

## Build

```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

## Userspace interface

| Interface | Description |
|-----------|-------------|
| /dev/bgt60atr24c | read / poll / ioctl |
| sysfs | state, counters |
| uapi | kernel/include/uapi/linux/bgt60atr24c.h |
| ioctl | START/STOP/RECOVER/GET/SET_CFG/GET_COUNTERS/GET_INFO |

## Optional tool

```bash
cd tools && make
./bgt60_capture -n 10
```

## License

GPL-2.0

## Contact

E-mail: zhchen2000@foxmail.com
