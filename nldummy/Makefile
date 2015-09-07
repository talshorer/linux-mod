obj-m := nldummy.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

all: check_uapi_netlink_value $(TEST) modules

clean: modules-clean bin-clean
