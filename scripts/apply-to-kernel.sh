#!/bin/sh
# Apply BGT60 QSPI driver sources into a Linux 4.1.15 kernel tree.
#
# Usage:
#   ./scripts/apply-to-kernel.sh /path/to/linux-kernel-4.1.15
#
set -e

if [ -z "$1" ]; then
	echo "Usage: $0 <kernel-src-dir>" >&2
	exit 1
fi

KSRC="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "$KSRC/Makefile" ]; then
	echo "Invalid kernel source: $KSRC" >&2
	exit 1
fi

echo "Applying BGT60 driver tree to $KSRC"

install -D -m 0644 "$ROOT/kernel/include/linux/fsl-quadspi.h" \
	"$KSRC/include/linux/fsl-quadspi.h"
install -D -m 0644 "$ROOT/kernel/include/uapi/linux/bgt60atr24c.h" \
	"$KSRC/include/uapi/linux/bgt60atr24c.h"
install -D -m 0644 "$ROOT/kernel/drivers/misc/bgt60atr24c.c" \
	"$KSRC/drivers/misc/bgt60atr24c.c"
install -D -m 0644 "$ROOT/kernel/drivers/misc/bgt60atr24c_regs.h" \
	"$KSRC/drivers/misc/bgt60atr24c_regs.h"
install -D -m 0644 "$ROOT/kernel/drivers/mtd/spi-nor/fsl-quadspi.c" \
	"$KSRC/drivers/mtd/spi-nor/fsl-quadspi.c"
install -D -m 0644 "$ROOT/kernel/arch/arm/boot/dts/imx6ull-bgt60atr24c-qspi.dtsi" \
	"$KSRC/arch/arm/boot/dts/imx6ull-bgt60atr24c-qspi.dtsi"
install -D -m 0644 \
	"$ROOT/kernel/Documentation/devicetree/bindings/misc/infineon,bgt60atr24c.txt" \
	"$KSRC/Documentation/devicetree/bindings/misc/infineon,bgt60atr24c.txt"

# Kconfig / Makefile snippets
if ! grep -q 'config BGT60ATR24C' "$KSRC/drivers/misc/Kconfig"; then
	sed -i '/^source "drivers\/misc\/cxl\/Kconfig"$/i\
config BGT60ATR24C\
\ttristate "Infineon BGT60ATR24C QSPI radar sensor"\
\tdepends on SPI_FSL_QUADSPI\
\tdepends on OF\
\thelp\
\t  Driver for Infineon BGT60ATR24C on i.MX6ULL QuadSPI.\
\
' "$KSRC/drivers/misc/Kconfig" 2>/dev/null || \
	python3 "$ROOT/scripts/merge-kconfig.py" "$KSRC/drivers/misc/Kconfig"
fi

if ! grep -q 'bgt60atr24c.o' "$KSRC/drivers/misc/Makefile"; then
	echo 'obj-$(CONFIG_BGT60ATR24C)	+= bgt60atr24c.o' >> "$KSRC/drivers/misc/Makefile"
fi

echo "Done. Enable in menuconfig:"
echo "  CONFIG_SPI_FSL_QUADSPI=y"
echo "  CONFIG_BGT60ATR24C=m"
