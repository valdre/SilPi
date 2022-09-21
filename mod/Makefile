LINUX ?= /lib/modules/`uname -r`/build

all:
	$(MAKE) -C $(LINUX) M=`pwd` modules
	
obj-m += SilPi.o
