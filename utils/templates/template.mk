M:=$(shell pwd)

include $(M)/../env.mk

all: modules

clean: modules-clean
