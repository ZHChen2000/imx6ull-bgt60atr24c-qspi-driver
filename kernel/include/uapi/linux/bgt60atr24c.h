/*
 * Userspace API for Infineon BGT60ATR24C driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _UAPI_LINUX_BGT60ATR24C_H
#define _UAPI_LINUX_BGT60ATR24C_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/const.h>

#define BGT60_FRAME_MAGIC	0x42475436 /* 'BGT6' */

#define BGT60_STATE_IDLE		0
#define BGT60_STATE_STREAMING		1
#define BGT60_STATE_ERROR		2

#define BGT60_FRAME_FLAG_FOF_ERR	BIT(0)
#define BGT60_FRAME_FLAG_BURST_ERR	BIT(1)
#define BGT60_FRAME_FLAG_CLK_NUM_ERR	BIT(2)

struct bgt60_frame_hdr {
	__u32 magic;
	__u32 seq;
	__u64 ts_ns;
	__u32 payload_len;
	__u32 flags;
};

struct bgt60_counters {
	__u64 frames;
	__u64 bytes;
	__u64 ring_overflow;
	__u64 user_drop;
	__u64 irq_count;
	__u64 qspi_timeout;
	__u64 fifo_overflow;
	__u64 burst_err;
	__u64 clk_num_err;
	__u64 controller_err;
};

struct bgt60_stream_cfg {
	__u32 fifo_cref;
	__u32 qspi_wait_cycles;
	__u32 lfsr_test;
	__u32 reserved;
};

struct bgt60_driver_info {
	__u32 version;
	__u32 state;
	__u32 fifo_cref;
	__u32 qspi_wait_cycles;
	__u32 lfsr_test;
	__u32 reserved;
};

#define BGT60_IOC_MAGIC		'B'

#define BGT60_IOC_START		_IO(BGT60_IOC_MAGIC, 0x01)
#define BGT60_IOC_STOP		_IO(BGT60_IOC_MAGIC, 0x02)
#define BGT60_IOC_RECOVER	_IO(BGT60_IOC_MAGIC, 0x03)
#define BGT60_IOC_GET_COUNTERS	_IOR(BGT60_IOC_MAGIC, 0x10, struct bgt60_counters)
#define BGT60_IOC_GET_CFG	_IOR(BGT60_IOC_MAGIC, 0x11, struct bgt60_stream_cfg)
#define BGT60_IOC_SET_CFG	_IOW(BGT60_IOC_MAGIC, 0x12, struct bgt60_stream_cfg)
#define BGT60_IOC_GET_INFO	_IOR(BGT60_IOC_MAGIC, 0x13, struct bgt60_driver_info)

#define BGT60_DRIVER_VERSION	1

#endif /* _UAPI_LINUX_BGT60ATR24C_H */
