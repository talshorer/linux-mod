obj-m := interceptor.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

all: check_uapi_o_value $(TEST) modules

clean: modules-clean bin-clean
