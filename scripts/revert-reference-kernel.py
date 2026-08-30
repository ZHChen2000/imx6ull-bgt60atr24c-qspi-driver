#!/usr/bin/env python3
"""Restore linux-kernel-4.1.15 reference tree (remove BGT driver edits)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "linux-kernel-4.1.15"

DELETE_FILES = [
    REF / "include/linux/fsl-quadspi.h",
    REF / "include/uapi/linux/bgt60atr24c.h",
    REF / "drivers/misc/bgt60atr24c.c",
    REF / "drivers/misc/bgt60atr24c_regs.h",
    REF / "arch/arm/boot/dts/imx6ull-bgt60atr24c-qspi.dtsi",
]

for p in DELETE_FILES:
    if p.exists():
        p.unlink()
        print("deleted", p.relative_to(ROOT))

# misc/Kconfig
kcfg = REF / "drivers/misc/Kconfig"
text = kcfg.read_text(encoding="utf-8", errors="replace")
text = re.sub(r"\nconfig BGT60ATR24C.*?help\n.*?\n\n", "\n", text, flags=re.S)
kcfg.write_text(text, encoding="utf-8")
print("reverted", kcfg.relative_to(ROOT))

# misc/Makefile
mk = REF / "drivers/misc/Makefile"
text = mk.read_text(encoding="utf-8", errors="replace")
text = text.replace("obj-$(CONFIG_BGT60ATR24C)\t+= bgt60atr24c.o\n", "")
mk.write_text(text, encoding="utf-8")
print("reverted", mk.relative_to(ROOT))

# dts/Makefile
dts_mk = REF / "arch/arm/boot/dts/Makefile"
text = dts_mk.read_text(encoding="utf-8", errors="replace")
text = text.replace("\timx6ull-bgt60atr24c-qspi.dtb \\\n", "")
dts_mk.write_text(text, encoding="utf-8")
print("reverted", dts_mk.relative_to(ROOT))

# fsl-quadspi.c
qspi = REF / "drivers/mtd/spi-nor/fsl-quadspi.c"
text = qspi.read_text(encoding="utf-8", errors="replace")
text = text.replace("#include <linux/fsl-quadspi.h>\n", "")
text = text.replace("#include <linux/of_platform.h>\n", "")
text = text.replace("#define SEQID_BGT_IP_OP\t\tSEQID_DYNAMIC_CMD0\n", "")

text = re.sub(
    r"\n\tbool has_bgt;\n\tbool bgt_only;\n};"
    r"\n\nstatic bool fsl_qspi_node_is_bgt.*?return false;\n}\n",
    "\n};\n",
    text,
    count=1,
    flags=re.S,
)

text = re.sub(
    r"\nstatic int fsl_qspi_trigger_seqid\(.*?\nEXPORT_SYMBOL_GPL\(fsl_qspi_exec_ip_op\);\n",
    "\n",
    text,
    count=1,
    flags=re.S,
)

old_probe = """\tq->nor_num = fsl_qspi_count_nor_children(dev->of_node);
\tq->has_bgt = fsl_qspi_has_bgt_child(dev->of_node);
\tq->bgt_only = q->has_bgt && !q->nor_num;

\tif (q->nor_num > FSL_QSPI_MAX_CHIP)
\t\treturn -ENODEV;

\tif (!q->nor_num && !q->has_bgt)
\t\treturn -ENODEV;"""

new_probe = """\tq->nor_num = of_get_child_count(dev->of_node);
\tif (!q->nor_num || q->nor_num > FSL_QSPI_MAX_CHIP)
\t\treturn -ENODEV;"""
text = text.replace(old_probe, new_probe)

old_setup = """\tif (q->bgt_only) {
\t\tu32 bgt_freq = 25000000;
\t\tstruct device_node *bgt_np;

\t\tbgt_np = of_get_compatible_child(dev->of_node,
\t\t\t\t\t\t \"infineon,bgt60atr24c\");
\t\tif (bgt_np) {
\t\t\tof_property_read_u32(bgt_np, \"qspi-max-frequency\",
\t\t\t\t\t     &bgt_freq);
\t\t\tof_node_put(bgt_np);
\t\t}

\t\tret = fsl_qspi_bgt_controller_setup(q, bgt_freq);
\t\tif (ret)
\t\t\tgoto irq_failed;
\t} else {
\t\tret = fsl_qspi_nor_setup(q);
\t\tif (ret)
\t\t\tgoto irq_failed;
\t}"""

text = text.replace(
    old_setup,
    "\tret = fsl_qspi_nor_setup(q);\n\tif (ret)\n\t\tgoto irq_failed;",
)

text = text.replace("\t/* iterate the NOR flash subnodes */\n",
                    "\t/* iterate the subnodes. */\n")
text = text.replace("\t\tif (fsl_qspi_node_is_bgt(np))\n\t\t\tcontinue;\n\n", "")

old_finish = """\t/* finish the rest init. */
\tif (q->nor_num) {
\t\tret = fsl_qspi_nor_setup_last(q);
\t\tif (ret)
\t\t\tgoto last_init_failed;
\t}

\tif (q->has_bgt) {
\t\tstruct device_node *child;

\t\tfor_each_available_child_of_node(dev->of_node, child) {
\t\t\tif (!fsl_qspi_node_is_bgt(child))
\t\t\t\tcontinue;
\t\t\tof_platform_device_create(child, NULL, dev);
\t\t}
\t}"""

text = text.replace(
    old_finish,
    "\t/* finish the rest init. */\n"
    "\tret = fsl_qspi_nor_setup_last(q);\n"
    "\tif (ret)\n"
    "\t\tgoto last_init_failed;",
)

qspi.write_text(text, encoding="utf-8")
print("reverted", qspi.relative_to(ROOT), "lines", text.count("\n"))
