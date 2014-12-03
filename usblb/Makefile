obj-m := usblb.o
usblb-y := usblb_gadget.o usblb_host.o usblb_main.o

M:=$(shell pwd)

include $(M)/../env.mk

all: modules

clean: modules-clean
