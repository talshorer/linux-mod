obj-m := echoserial.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

# dependencies order is important here, since it
# decides the argument order to the shell script
check_uapi_port_value: check_uapi_port_value.sh $(notdir $(M))_uapi.h
	./$^ $(KERNEL)

all: check_uapi_port_value $(TEST) modules

clean: modules-clean bin-clean
