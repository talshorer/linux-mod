obj-m := virtblock.o

M:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(M)/../env.mk

all: modules

clean: modules-clean
