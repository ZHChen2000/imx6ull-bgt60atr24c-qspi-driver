CC ?= gcc
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ifdef KERNEL_SRC
UAPI_INC := $(KERNEL_SRC)/include/uapi
else
UAPI_INC := $(ROOT)/kernel/include/uapi
endif
CFLAGS ?= -O2 -Wall
CFLAGS += -I$(UAPI_INC)

all: bgt60_capture

bgt60_capture: bgt60_capture.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bgt60_capture

.PHONY: all clean
