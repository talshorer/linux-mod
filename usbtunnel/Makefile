obj-m := usbtunnel.o

M:=$(shell pwd)

ccflags-y += -I$(srctree)/drivers/usb/gadget/

include $(M)/../env.mk

all: modules

clean: modules-clean
