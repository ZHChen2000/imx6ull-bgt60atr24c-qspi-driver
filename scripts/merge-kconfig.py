#!/usr/bin/env python3
"""Insert BGT60ATR24C Kconfig entry before cxl Kconfig source."""
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
block = '''
config BGT60ATR24C
\ttristate "Infineon BGT60ATR24C QSPI radar sensor"
\tdepends on SPI_FSL_QUADSPI
\tdepends on OF
\thelp
\t  Driver for Infineon BGT60ATR24C on i.MX6ULL QuadSPI.

'''
needle = 'source "drivers/misc/cxl/Kconfig"'
if 'config BGT60ATR24C' not in text and needle in text:
    text = text.replace(needle, block + needle)
    path.write_text(text, encoding="utf-8")
    print("updated", path)
