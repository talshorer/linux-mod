obj-m := virtnet.o

virtnet-y := virtnet_net.o

include $(PWD)/../env.mk

all:
	make -C $(KERNEL) M=$(PWD) modules

clean:
	make -C $(KERNEL) M=$(PWD) clean

