/*
 * Freescale QuadSPI controller - BGT60 extension API
 *
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef LINUX_FSL_QUADSPI_H
#define LINUX_FSL_QUADSPI_H

#include <linux/types.h>

struct device;
struct fsl_qspi;

/**
 * struct fsl_qspi_ip_op - describe one IP Command transaction for BGT60
 * @cmd:        PAD1 SPI command byte, or PAD4 QSPI 8-bit address
 * @cmd_buswidth: command/address phase bus width (1 or 4)
 * @data_buswidth: data phase bus width (1 or 4)
 * @dummy_cycles: QSPI WAIT cycles (PAD4 only)
 * @data_in:    true = controller receives data into @buf
 * @nbytes:     number of payload bytes (IPCR length)
 * @buf:        TX/RX buffer (may be NULL for zero-length ops)
 */
struct fsl_qspi_ip_op {
	u8 cmd;
	u8 cmd_buswidth;
	u8 data_buswidth;
	u8 dummy_cycles;
	bool data_in;
	size_t nbytes;
	void *buf;
};

#if IS_ENABLED(CONFIG_SPI_FSL_QUADSPI)

struct fsl_qspi *fsl_qspi_get_controller(struct device *dev);
int fsl_qspi_exec_ip_op(struct fsl_qspi *q, const struct fsl_qspi_ip_op *op);
int fsl_qspi_exec_ip_read(struct fsl_qspi *q, const struct fsl_qspi_ip_op *op);

#else

static inline struct fsl_qspi *fsl_qspi_get_controller(struct device *dev)
{
	return NULL;
}

static inline int fsl_qspi_exec_ip_op(struct fsl_qspi *q,
				      const struct fsl_qspi_ip_op *op)
{
	return -ENODEV;
}

static inline int fsl_qspi_exec_ip_read(struct fsl_qspi *q,
					const struct fsl_qspi_ip_op *op)
{
	return -ENODEV;
}

#endif /* CONFIG_SPI_FSL_QUADSPI */

#endif /* LINUX_FSL_QUADSPI_H */
