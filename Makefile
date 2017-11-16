obj-m = dht22.o

KPATH=~/RPi3_Workshop/kernel_src/linux
PWD=$(shell pwd)
CROSS=~/RPi3_Workshop/tool_chain/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-
CC=$(CROSS)gcc

all: dht22.ko poll

dht22.ko:
	make -C $(KPATH) ARCH=arm CROSS_COMPILE=$(CROSS) SUBDIRS=$(PWD) modules

poll: poll.o
	$(CC) poll.o -lc -lpthread -o poll

poll.o: poll.c
	$(CC) -c poll.c

clean:
	rm -rf *.o *.ko .*cmd .tmp* core *.i *.mod.c modules.* Module.* poll
