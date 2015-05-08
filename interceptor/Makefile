obj-m := interceptor.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

# dependencies order is important here, since it
# decides the argument order to the shell script
check_uapi_o_value: check_uapi_o_value.sh $(notdir $(M))_uapi.h
	./$^

all: check_uapi_o_value $(TEST) modules

clean: modules-clean bin-clean
