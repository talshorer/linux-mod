obj-m := seriorepeater.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

all: check_uapi_proto_value $(TEST) modules

clean: modules-clean bin-clean
