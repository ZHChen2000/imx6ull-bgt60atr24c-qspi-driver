/*
 * Infineon BGT60ATR24C QSPI radar data acquisition driver
 *
 * Copyright (C) 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>
#include <linux/fsl-quadspi.h>
#include <uapi/linux/bgt60atr24c.h>

#include "bgt60atr24c_regs.h"

#define BGT60_DRIVER_NAME		"bgt60atr24c"
#define BGT60_DEVICE_NAME		"bgt60atr24c"
#define BGT60_RING_SIZE_DEFAULT		(256 * 1024)
#define BGT60_MAX_FRAME_PAYLOAD		(8192 * 3)
struct bgt60_dev {
	struct device *dev;
	struct fsl_qspi *qspi;
	struct miscdevice miscdev;
	struct gpio_desc *reset_gpio;
	int irq;

	struct mutex lock;
	spinlock_t ring_lock;
	wait_queue_head_t waitq;

	int state;
	u32 fifo_cref;
	u32 qspi_wait;
	bool lfsr_test;

	DECLARE_KFIFO(ring, u8, BGT60_RING_SIZE_DEFAULT);
	size_t ring_frame_bytes;

	u32 frame_seq;
	struct bgt60_counters counters;
};

static u32 bgt60_unpack_be24(const u8 *buf)
{
	return ((u32)buf[0] << 16) | ((u32)buf[1] << 8) | buf[2];
}

static void bgt60_pack_be24(u32 val, u8 *buf)
{
	buf[0] = (val >> 16) & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = val & 0xff;
}

static u8 bgt60_spi_cmd(u8 reg, bool write)
{
	return (reg << 1) | (write ? 0 : 1);
}

static int bgt60_reg_read(struct bgt60_dev *bgt, u8 reg, u32 *val)
{
	struct fsl_qspi_ip_op op;
	u8 rx[BGT60_REG_WIDTH];
	int ret;

	memset(&op, 0, sizeof(op));
	op.cmd = bgt60_spi_cmd(reg, false);
	op.cmd_buswidth = 1;
	op.data_buswidth = 1;
	op.data_in = true;
	op.nbytes = BGT60_REG_WIDTH;
	op.buf = rx;

	ret = fsl_qspi_exec_ip_op(bgt->qspi, &op);
	if (ret) {
		bgt->counters.controller_err++;
		return ret;
	}

	*val = bgt60_unpack_be24(rx);
	return 0;
}

static int bgt60_reg_write(struct bgt60_dev *bgt, u8 reg, u32 val)
{
	struct fsl_qspi_ip_op op;
	u8 tx[BGT60_REG_WIDTH];
	int ret;

	bgt60_pack_be24(val, tx);

	memset(&op, 0, sizeof(op));
	op.cmd = bgt60_spi_cmd(reg, true);
	op.cmd_buswidth = 1;
	op.data_buswidth = 1;
	op.data_in = false;
	op.nbytes = BGT60_REG_WIDTH;
	op.buf = tx;

	ret = fsl_qspi_exec_ip_op(bgt->qspi, &op);
	if (ret)
		bgt->counters.controller_err++;

	return ret;
}

static int bgt60_qspi_read(struct bgt60_dev *bgt, u8 addr, u8 *buf, size_t len)
{
	struct fsl_qspi_ip_op op;
	int ret;

	if (addr >= BGT60_QSPI_ADDR_MAX)
		return -EINVAL;

	memset(&op, 0, sizeof(op));
	op.cmd = addr;
	op.cmd_buswidth = 4;
	op.data_buswidth = 4;
	op.dummy_cycles = bgt->qspi_wait;
	op.data_in = true;
	op.nbytes = len;
	op.buf = buf;

	ret = fsl_qspi_exec_ip_read(bgt->qspi, &op);
	if (ret)
		bgt->counters.controller_err++;

	return ret;
}

static int bgt60_hw_reset(struct bgt60_dev *bgt)
{
	if (!bgt->reset_gpio)
		return 0;

	gpiod_set_value(bgt->reset_gpio, 1);
	udelay(1);
	gpiod_set_value(bgt->reset_gpio, 0);
	udelay(1);
	gpiod_set_value(bgt->reset_gpio, 1);
	udelay(1);

	return 0;
}

static int bgt60_main_w1c(struct bgt60_dev *bgt, u32 mask)
{
	u32 mainreg;
	int ret;

	ret = bgt60_reg_read(bgt, BGT60_REG_MAIN, &mainreg);
	if (ret)
		return ret;

	mainreg |= mask;
	return bgt60_reg_write(bgt, BGT60_REG_MAIN, mainreg);
}

static int bgt60_fifo_reset(struct bgt60_dev *bgt)
{
	int ret;

	ret = bgt60_main_w1c(bgt, BGT60_MAIN_FIFO_RESET);
	if (ret)
		return ret;

	udelay(1);
	return 0;
}

static u32 bgt60_sample_fstat_flags(struct bgt60_dev *bgt)
{
	u32 fstat = 0;
	u32 flags = 0;

	if (bgt60_reg_read(bgt, BGT60_REG_FSTAT, &fstat))
		return 0;

	if (fstat & BGT60_FSTAT_FOF_ERR) {
		bgt->counters.fifo_overflow++;
		flags |= BGT60_FRAME_FLAG_FOF_ERR;
	}
	if (fstat & BGT60_FSTAT_BURST_ERR) {
		bgt->counters.burst_err++;
		flags |= BGT60_FRAME_FLAG_BURST_ERR;
	}
	if (fstat & BGT60_FSTAT_CLK_NUM_ERR) {
		bgt->counters.clk_num_err++;
		flags |= BGT60_FRAME_FLAG_CLK_NUM_ERR;
	}

	return flags;
}

static int bgt60_read_chip_id(struct bgt60_dev *bgt, u64 *chip_id)
{
	u32 id1, id2;
	int ret;

	ret = bgt60_reg_read(bgt, BGT60_REG_CHIP_ID1, &id1);
	if (ret)
		return ret;

	ret = bgt60_reg_read(bgt, BGT60_REG_CHIP_ID2, &id2);
	if (ret)
		return ret;

	*chip_id = ((u64)id2 << 24) | id1;
	return 0;
}

static void bgt60_ring_reset(struct bgt60_dev *bgt)
{
	unsigned long irqflags;

	spin_lock_irqsave(&bgt->ring_lock, irqflags);
	kfifo_reset(&bgt->ring);
	spin_unlock_irqrestore(&bgt->ring_lock, irqflags);
}

static int bgt60_apply_sfctl(struct bgt60_dev *bgt)
{
	u32 sfctl = 0;

	sfctl |= (bgt->qspi_wait << BGT60_SFCTL_QSPI_WT_SHIFT) &
		 BGT60_SFCTL_QSPI_WT_MASK;
	sfctl |= bgt->fifo_cref & BGT60_SFCTL_FIFO_CREF_MASK;
	if (bgt->lfsr_test)
		sfctl |= BGT60_SFCTL_LFSR_EN;

	return bgt60_reg_write(bgt, BGT60_REG_SFCTL, sfctl);
}

static void bgt60_recalc_frame_bytes(struct bgt60_dev *bgt)
{
	bgt->ring_frame_bytes = sizeof(struct bgt60_frame_hdr) +
				(size_t)bgt->fifo_cref * BGT60_REG_WIDTH;
}

static int bgt60_ring_push_frame(struct bgt60_dev *bgt, const u8 *payload,
				 size_t payload_len, u32 flags)
{
	struct bgt60_frame_hdr hdr;
	unsigned long irqflags;
	size_t total;
	int ret;

	if (payload_len > BGT60_MAX_FRAME_PAYLOAD)
		return -EMSGSIZE;

	hdr.magic = BGT60_FRAME_MAGIC;
	hdr.seq = bgt->frame_seq++;
	hdr.ts_ns = ktime_get_ns();
	hdr.payload_len = payload_len;
	hdr.flags = flags;
	total = sizeof(hdr) + payload_len;

	spin_lock_irqsave(&bgt->ring_lock, irqflags);
	if (kfifo_avail(&bgt->ring) < total) {
		bgt->counters.ring_overflow++;
		spin_unlock_irqrestore(&bgt->ring_lock, irqflags);
		return -ENOSPC;
	}

	ret = kfifo_in(&bgt->ring, &hdr, sizeof(hdr));
	if (ret != sizeof(hdr)) {
		spin_unlock_irqrestore(&bgt->ring_lock, irqflags);
		return -EIO;
	}

	ret = kfifo_in(&bgt->ring, payload, payload_len);
	spin_unlock_irqrestore(&bgt->ring_lock, irqflags);
	if (ret != payload_len)
		return -EIO;

	bgt->counters.frames++;
	bgt->counters.bytes += payload_len;
	wake_up_interruptible(&bgt->waitq);

	return 0;
}

static void bgt60_drain_fifo(struct bgt60_dev *bgt)
{
	size_t chunk_bytes;
	u8 *buf;
	u32 flags;
	int ret;

	if (bgt->fifo_cref == 0)
		return;

	chunk_bytes = (size_t)bgt->fifo_cref * BGT60_REG_WIDTH;
	buf = kmalloc(chunk_bytes, GFP_KERNEL);
	if (!buf)
		return;

	ret = bgt60_qspi_read(bgt, BGT60_FIFO_ADDR, buf, chunk_bytes);
	if (ret) {
		bgt->counters.qspi_timeout++;
		bgt->state = BGT60_STATE_ERROR;
		kfree(buf);
		return;
	}

	flags = bgt60_sample_fstat_flags(bgt);
	if (bgt60_ring_push_frame(bgt, buf, chunk_bytes, flags))
		bgt->counters.user_drop++;

	kfree(buf);
}

static int bgt60_do_stop(struct bgt60_dev *bgt)
{
	bgt->state = BGT60_STATE_IDLE;
	bgt60_ring_reset(bgt);
	return 0;
}

static int bgt60_do_start(struct bgt60_dev *bgt)
{
	int ret;

	if (bgt->state == BGT60_STATE_STREAMING)
		return 0;

	ret = bgt60_apply_sfctl(bgt);
	if (ret)
		return ret;

	bgt->state = BGT60_STATE_STREAMING;
	return 0;
}

static int bgt60_do_recover(struct bgt60_dev *bgt)
{
	int ret;

	bgt60_do_stop(bgt);

	ret = bgt60_fifo_reset(bgt);
	if (ret)
		return ret;

	ret = bgt60_hw_reset(bgt);
	if (ret)
		return ret;

	bgt->state = BGT60_STATE_IDLE;
	return 0;
}

static irqreturn_t bgt60_irq_thread(int irq, void *data)
{
	struct bgt60_dev *bgt = data;

	if (bgt->state != BGT60_STATE_STREAMING)
		return IRQ_HANDLED;

	bgt->counters.irq_count++;
	bgt60_drain_fifo(bgt);

	return IRQ_HANDLED;
}

static irqreturn_t bgt60_irq_handler(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

static ssize_t bgt60_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct bgt60_dev *bgt = filp->private_data;
	unsigned long irqflags;
	size_t copied = 0;
	int ret;

	if (bgt->state == BGT60_STATE_ERROR)
		return -EIO;

	if (count < bgt->ring_frame_bytes)
		return -EINVAL;

	if (mutex_lock_interruptible(&bgt->lock))
		return -ERESTARTSYS;

	while (kfifo_is_empty(&bgt->ring)) {
		mutex_unlock(&bgt->lock);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(bgt->waitq,
				!kfifo_is_empty(&bgt->ring) ||
				bgt->state == BGT60_STATE_ERROR);
		if (ret)
			return ret;
		if (bgt->state == BGT60_STATE_ERROR)
			return -EIO;
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
	}

	spin_lock_irqsave(&bgt->ring_lock, irqflags);
	ret = kfifo_to_user(&bgt->ring, buf, bgt->ring_frame_bytes, &copied);
	spin_unlock_irqrestore(&bgt->ring_lock, irqflags);
	mutex_unlock(&bgt->lock);

	if (ret) {
		bgt->counters.user_drop++;
		return ret;
	}

	return copied;
}

static unsigned int bgt60_poll(struct file *filp, poll_table *wait)
{
	struct bgt60_dev *bgt = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &bgt->waitq, wait);

	if (bgt->state == BGT60_STATE_ERROR)
		mask |= POLLERR;
	if (!kfifo_is_empty(&bgt->ring))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static long bgt60_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct bgt60_dev *bgt = filp->private_data;
	struct bgt60_stream_cfg cfg;
	struct bgt60_driver_info info;
	int ret = 0;

	switch (cmd) {
	case BGT60_IOC_START:
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
		ret = bgt60_do_start(bgt);
		mutex_unlock(&bgt->lock);
		break;
	case BGT60_IOC_STOP:
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
		ret = bgt60_do_stop(bgt);
		mutex_unlock(&bgt->lock);
		break;
	case BGT60_IOC_RECOVER:
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
		ret = bgt60_do_recover(bgt);
		mutex_unlock(&bgt->lock);
		break;
	case BGT60_IOC_GET_COUNTERS:
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
		bgt60_sample_fstat_flags(bgt);
		if (copy_to_user((void __user *)arg, &bgt->counters,
				 sizeof(bgt->counters)))
			ret = -EFAULT;
		mutex_unlock(&bgt->lock);
		break;
	case BGT60_IOC_GET_CFG:
		cfg.fifo_cref = bgt->fifo_cref;
		cfg.qspi_wait_cycles = bgt->qspi_wait;
		cfg.lfsr_test = bgt->lfsr_test ? 1 : 0;
		cfg.reserved = 0;
		if (copy_to_user((void __user *)arg, &cfg, sizeof(cfg)))
			ret = -EFAULT;
		break;
	case BGT60_IOC_SET_CFG:
		if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
			return -EFAULT;
		if (mutex_lock_interruptible(&bgt->lock))
			return -ERESTARTSYS;
		if (bgt->state != BGT60_STATE_IDLE) {
			mutex_unlock(&bgt->lock);
			return -EBUSY;
		}
		if (cfg.fifo_cref == 0 || cfg.fifo_cref > 8191) {
			mutex_unlock(&bgt->lock);
			return -EINVAL;
		}
		bgt->fifo_cref = cfg.fifo_cref;
		bgt->qspi_wait = cfg.qspi_wait_cycles & 0xf;
		bgt->lfsr_test = cfg.lfsr_test ? true : false;
		bgt60_recalc_frame_bytes(bgt);
		mutex_unlock(&bgt->lock);
		break;
	case BGT60_IOC_GET_INFO:
		info.version = BGT60_DRIVER_VERSION;
		info.state = bgt->state;
		info.fifo_cref = bgt->fifo_cref;
		info.qspi_wait_cycles = bgt->qspi_wait;
		info.lfsr_test = bgt->lfsr_test ? 1 : 0;
		info.reserved = 0;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	default:
		return -ENOTTY;
	}

	return ret;
}

static int bgt60_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *misc = filp->private_data;
	struct bgt60_dev *bgt = dev_get_drvdata(misc->parent);

	if (!bgt || !bgt->qspi)
		return -ENODEV;

	filp->private_data = bgt;
	return nonseekable_open(inode, filp);
}

static const struct file_operations bgt60_fops = {
	.owner		= THIS_MODULE,
	.open		= bgt60_open,
	.read		= bgt60_read,
	.poll		= bgt60_poll,
	.unlocked_ioctl	= bgt60_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bgt60_ioctl,
#endif
};

static ssize_t bgt60_sysfs_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct bgt60_dev *bgt = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bgt->state);
}

static ssize_t bgt60_sysfs_counters_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bgt60_dev *bgt = dev_get_drvdata(dev);
	struct bgt60_counters c;

	mutex_lock(&bgt->lock);
	bgt60_sample_fstat_flags(bgt);
	memcpy(&c, &bgt->counters, sizeof(c));
	mutex_unlock(&bgt->lock);

	return scnprintf(buf, PAGE_SIZE,
			 "frames %llu\nbytes %llu\nring_overflow %llu\n"
			 "user_drop %llu\nirq_count %llu\nqspi_timeout %llu\n"
			 "fifo_overflow %llu\nburst_err %llu\n"
			 "clk_num_err %llu\ncontroller_err %llu\n",
			 (unsigned long long)c.frames,
			 (unsigned long long)c.bytes,
			 (unsigned long long)c.ring_overflow,
			 (unsigned long long)c.user_drop,
			 (unsigned long long)c.irq_count,
			 (unsigned long long)c.qspi_timeout,
			 (unsigned long long)c.fifo_overflow,
			 (unsigned long long)c.burst_err,
			 (unsigned long long)c.clk_num_err,
			 (unsigned long long)c.controller_err);
}

static DEVICE_ATTR(state, 0444, bgt60_sysfs_state_show, NULL);
static DEVICE_ATTR(counters, 0444, bgt60_sysfs_counters_show, NULL);

static struct attribute *bgt60_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_counters.attr,
	NULL,
};

static const struct attribute_group bgt60_attr_group = {
	.attrs = bgt60_attrs,
};

static int bgt60_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bgt60_dev *bgt;
	u64 chip_id = 0;
	u32 chip_version = 0;
	int ret;

	bgt = devm_kzalloc(dev, sizeof(*bgt), GFP_KERNEL);
	if (!bgt)
		return -ENOMEM;

	bgt->dev = dev;
	bgt->state = BGT60_STATE_IDLE;
	bgt->fifo_cref = 512;
	bgt->qspi_wait = 7;
	mutex_init(&bgt->lock);
	spin_lock_init(&bgt->ring_lock);
	init_waitqueue_head(&bgt->waitq);
	INIT_KFIFO(bgt->ring);

	of_property_read_u32(dev->of_node, "fifo-cref", &bgt->fifo_cref);
	of_property_read_u32(dev->of_node, "qspi-wait-cycles", &bgt->qspi_wait);
	if (of_property_read_bool(dev->of_node, "lfsr-test"))
		bgt->lfsr_test = true;

	bgt->qspi = fsl_qspi_get_controller(dev);
	if (!bgt->qspi) {
		dev_err(dev, "failed to get QSPI controller\n");
		return -EPROBE_DEFER;
	}

	bgt->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(bgt->reset_gpio))
		return PTR_ERR(bgt->reset_gpio);

	ret = bgt60_hw_reset(bgt);
	if (ret)
		return ret;

	ret = bgt60_read_chip_id(bgt, &chip_id);
	if (ret) {
		dev_err(dev, "CHIP_ID read failed: %d\n", ret);
		return ret;
	}

	ret = bgt60_reg_read(bgt, BGT60_REG_CHIP_VERSION, &chip_version);
	if (ret) {
		dev_err(dev, "CHIP_VERSION read failed: %d\n", ret);
		return ret;
	}

	dev_info(dev, "CHIP_ID=0x%012llx CHIP_VERSION=0x%06x\n",
		 chip_id, chip_version);

	bgt->irq = platform_get_irq(pdev, 0);
	if (bgt->irq < 0)
		return bgt->irq;

	ret = devm_request_threaded_irq(dev, bgt->irq, bgt60_irq_handler,
					bgt60_irq_thread, IRQF_ONESHOT,
					BGT60_DRIVER_NAME, bgt);
	if (ret)
		return ret;

	bgt60_recalc_frame_bytes(bgt);

	ret = sysfs_create_group(&dev->kobj, &bgt60_attr_group);
	if (ret)
		return ret;

	bgt->miscdev.minor = MISC_DYNAMIC_MINOR;
	bgt->miscdev.name = BGT60_DEVICE_NAME;
	bgt->miscdev.fops = &bgt60_fops;
	bgt->miscdev.parent = dev;

	platform_set_drvdata(pdev, bgt);

	ret = misc_register(&bgt->miscdev);
	if (ret) {
		sysfs_remove_group(&dev->kobj, &bgt60_attr_group);
		return ret;
	}

	return 0;
}

static int bgt60_remove(struct platform_device *pdev)
{
	struct bgt60_dev *bgt = platform_get_drvdata(pdev);

	if (bgt) {
		mutex_lock(&bgt->lock);
		bgt60_do_stop(bgt);
		mutex_unlock(&bgt->lock);
		misc_deregister(&bgt->miscdev);
		sysfs_remove_group(&pdev->dev.kobj, &bgt60_attr_group);
	}

	return 0;
}

static const struct of_device_id bgt60_of_match[] = {
	{ .compatible = "infineon,bgt60atr24c" },
	{ }
};
MODULE_DEVICE_TABLE(of, bgt60_of_match);

static struct platform_driver bgt60_driver = {
	.probe	= bgt60_probe,
	.remove	= bgt60_remove,
	.driver = {
		.name = BGT60_DRIVER_NAME,
		.of_match_table = bgt60_of_match,
	},
};
module_platform_driver(bgt60_driver);

MODULE_DESCRIPTION("Infineon BGT60ATR24C QSPI radar driver");
MODULE_AUTHOR("Infineon BGT60ATR24C driver contributors");
MODULE_LICENSE("GPL v2");
