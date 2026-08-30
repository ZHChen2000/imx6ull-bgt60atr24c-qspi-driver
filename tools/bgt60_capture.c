/*
 * Minimal userspace capture example for /dev/bgt60atr24c
 *
 * Build (with kernel headers installed):
 *   ${CC} -O2 -o bgt60_capture bgt60_capture.c
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/bgt60atr24c.h>

#define DEV_PATH "/dev/bgt60atr24c"

static void print_counters(int fd)
{
	struct bgt60_counters ctr;

	if (ioctl(fd, BGT60_IOC_GET_COUNTERS, &ctr) < 0) {
		perror("GET_COUNTERS");
		return;
	}

	printf("frames=%llu bytes=%llu ring_ovf=%llu user_drop=%llu\n",
	       (unsigned long long)ctr.frames,
	       (unsigned long long)ctr.bytes,
	       (unsigned long long)ctr.ring_overflow,
	       (unsigned long long)ctr.user_drop);
	printf("irq=%llu qspi_to=%llu fifo_ovf=%llu burst=%llu clk=%llu ctrl=%llu\n",
	       (unsigned long long)ctr.irq_count,
	       (unsigned long long)ctr.qspi_timeout,
	       (unsigned long long)ctr.fifo_overflow,
	       (unsigned long long)ctr.burst_err,
	       (unsigned long long)ctr.clk_num_err,
	       (unsigned long long)ctr.controller_err);
}

int main(int argc, char **argv)
{
	int fd, frames = 10, outfd = 1;
	char *outpath = NULL;
	struct pollfd pfd;
	char *framebuf;
	size_t frame_cap = 64 * 1024;
	int i, ret, opt;

	while ((opt = getopt(argc, argv, "n:o:h")) != -1) {
		switch (opt) {
		case 'n':
			frames = atoi(optarg);
			break;
		case 'o':
			outpath = optarg;
			break;
		default:
			fprintf(stderr, "Usage: %s [-n frames] [-o file]\n",
				argv[0]);
			return 1;
		}
	}

	framebuf = malloc(frame_cap);
	if (!framebuf)
		return 1;

	fd = open(DEV_PATH, O_RDONLY);
	if (fd < 0) {
		perror(DEV_PATH);
		free(framebuf);
		return 1;
	}

	if (outpath) {
		outfd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (outfd < 0) {
			perror(outpath);
			close(fd);
			free(framebuf);
			return 1;
		}
	}

	if (ioctl(fd, BGT60_IOC_START) < 0) {
		perror("START");
		close(fd);
		free(framebuf);
		return 1;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;

	for (i = 0; i < frames; i++) {
		ret = poll(&pfd, 1, 5000);
		if (ret <= 0) {
			fprintf(stderr, "poll timeout/error at frame %d\n", i);
			break;
		}

		ret = read(fd, framebuf, frame_cap);
		if (ret < 0) {
			perror("read");
			break;
		}

		if (write(outfd, framebuf, ret) != ret) {
			perror("write");
			break;
		}
	}

	ioctl(fd, BGT60_IOC_STOP);
	print_counters(fd);

	close(fd);
	if (outpath)
		close(outfd);
	free(framebuf);
	return 0;
}
