obj-m := simplat.o simplat-device.o

include $(PWD)/../env.mk

all:
	make -C $(KERNEL) M=$(PWD) modules

clean:
	make -C $(KERNEL) M=$(PWD) clean

