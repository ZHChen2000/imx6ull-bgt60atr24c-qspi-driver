#!/usr/bin/env python3
"""Generate patch series under patches/ from kernel/ vs linux-kernel-4.1.15/."""
import difflib
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "linux-kernel-4.1.15"
SRC = ROOT / "kernel"
OUT = ROOT / "patches"

PAIRS = [
    ("drivers/mtd/spi-nor/fsl-quadspi.c", "0001-qspi-add-ip-op.patch"),
    ("drivers/misc/bgt60atr24c.c", "0002-bgt60atr24c-driver.patch"),
    ("drivers/misc/bgt60atr24c_regs.h", "0002-bgt60atr24c-driver.patch"),
    ("include/linux/fsl-quadspi.h", "0002-bgt60atr24c-driver.patch"),
    ("include/uapi/linux/bgt60atr24c.h", "0002-bgt60atr24c-driver.patch"),
    (
        "Documentation/devicetree/bindings/misc/infineon,bgt60atr24c.txt",
        "0002-bgt60atr24c-driver.patch",
    ),
    ("arch/arm/boot/dts/imx6ull-bgt60atr24c-qspi.dtsi", "0003-dts-enable-bgt60.patch"),
]

KCONFIG_SNIPPET = '''
config BGT60ATR24C
\ttristate "Infineon BGT60ATR24C QSPI radar sensor"
\tdepends on SPI_FSL_QUADSPI
\tdepends on OF
\thelp
\t  Driver for Infineon BGT60ATR24C on i.MX6ULL QuadSPI.
'''

MAKEFILE_SNIPPET = "obj-$(CONFIG_BGT60ATR24C)\t+= bgt60atr24c.o\n"


def udiff(old_path, new_path, rel):
    old_lines = []
    if old_path.exists():
        old_lines = old_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    new_lines = new_path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    if old_lines == new_lines:
        return []
    tag = "a/" + rel.replace("\\", "/")
    return list(difflib.unified_diff(
        old_lines, new_lines, fromfile=tag, tofile=tag.replace("a/", "b/", 1), n=3))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    buckets = {}

    for rel, name in PAIRS:
        old_p = REF / rel
        new_p = SRC / rel
        if not new_p.exists():
            continue
        chunk = udiff(old_p, new_p, rel)
        if chunk:
            buckets.setdefault(name, []).extend(chunk)

    # Kconfig / Makefile / dts Makefile fragments go to patch 2/3
    kcfg = REF / "drivers/misc/Kconfig"
    ktext = kcfg.read_text(encoding="utf-8", errors="replace")
    if "config BGT60ATR24C" not in ktext:
        needle = 'source "drivers/misc/cxl/Kconfig"\n'
        if needle in ktext:
            newtext = ktext.replace(needle, KCONFIG_SNIPPET + "\n" + needle)
            chunk = list(difflib.unified_diff(
                ktext.splitlines(keepends=True),
                newtext.splitlines(keepends=True),
                fromfile="a/drivers/misc/Kconfig",
                tofile="b/drivers/misc/Kconfig", n=3))
            buckets.setdefault("0002-bgt60atr24c-driver.patch", []).extend(chunk)

    mk = REF / "drivers/misc/Makefile"
    mtext = mk.read_text(encoding="utf-8", errors="replace")
    if "bgt60atr24c.o" not in mtext:
        newtext = mtext + MAKEFILE_SNIPPET
        chunk = list(difflib.unified_diff(
            mtext.splitlines(keepends=True),
            newtext.splitlines(keepends=True),
            fromfile="a/drivers/misc/Makefile",
            tofile="b/drivers/misc/Makefile", n=3))
        buckets.setdefault("0002-bgt60atr24c-driver.patch", []).extend(chunk)

    for name, lines in sorted(buckets.items()):
        out = OUT / name
        out.write_text("".join(lines), encoding="utf-8")
        print("wrote", out.name, "lines", len(lines))

if __name__ == "__main__":
    main()
