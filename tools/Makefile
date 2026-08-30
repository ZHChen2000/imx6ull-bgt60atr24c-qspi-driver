CC ?= gcc
CFLAGS ?= -O2 -Wall

all: bgt60_capture

bgt60_capture: bgt60_capture.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bgt60_capture

.PHONY: all clean
