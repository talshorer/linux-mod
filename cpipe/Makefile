obj-m := cpipe.o

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all:
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) modules

clean:
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) clean

