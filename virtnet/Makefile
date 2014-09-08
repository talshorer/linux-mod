BACKEND ?= lb
ccflags-y += -DVIRTNET_BACKEND=$(BACKEND)
obj-m := virtnet.o
virtnet-y := virtnet_net.o virtnet_$(BACKEND).o

include $(PWD)/../env.mk

all:
	make -C $(KERNEL) M=$(PWD) modules

clean:
	make -C $(KERNEL) M=$(PWD) clean
