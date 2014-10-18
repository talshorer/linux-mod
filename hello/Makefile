obj-m := hello.o

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: modules

clean: modules-clean
