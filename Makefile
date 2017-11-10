obj-m = dht22_exp.o

KPATH=/home/edwardlin/RPi3_Workshop/kernel_src/linux
PWD=$(shell pwd)
CROSS=~/RPi3_Workshop/tool_chain/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-

all:
	make -C $(KPATH) ARCH=arm CROSS_COMPILE=$(CROSS) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.o *.ko .*cmd .tmp* core *.i *.mod.c modules.* Module.*
