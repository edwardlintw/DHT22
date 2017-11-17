obj-m = dht22.o

KPATH=~/RPi3_Workshop/kernel_src/linux
PWD=$(shell pwd)
CROSS=~/RPi3_Workshop/tool_chain/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-
CC=$(CROSS)gcc

all: dht22 poll

dht22: dht22.c dht22.h
	make -C $(KPATH) ARCH=arm CROSS_COMPILE=$(CROSS) SUBDIRS=$(PWD) modules

poll: poll.c
	$(CC) -o poll -lc -lpthread poll.c

clean:
	rm -rf *.o *.ko .*cmd .tmp* core *.i *.mod.c modules.* Module.* poll
