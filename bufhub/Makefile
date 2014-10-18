obj-m := bufhub.o

TEST=test.out

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: $(TEST) modules

clean: modules-clean
	rm -f $(TEST)
