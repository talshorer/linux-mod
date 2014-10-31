obj-m := echoserial.o

TEST=test.out

M:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(M)/../env.mk

all: $(TEST) modules

clean: modules-clean
	rm -f $(TEST)
