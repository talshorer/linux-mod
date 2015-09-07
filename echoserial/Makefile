obj-m := echoserial.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

all: check_uapi_port_value $(TEST) modules

clean: modules-clean bin-clean
