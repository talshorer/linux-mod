obj-m := usblb.o
usblb-y := usblb_gadget.o usblb_host.o usblb_main.o usblb_glue.o

M:=$(shell pwd)

include $(M)/../env.mk

all: modules

clean: modules-clean
