obj-m := interceptor.o

TEST=test.out

M:=$(shell pwd)

include $(M)/../env.mk

all: $(TEST) modules

clean: modules-clean bin-clean
