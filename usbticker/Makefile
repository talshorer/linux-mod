obj-m := usb_f_ticker.o ticker.o
usb_f_ticker-y := f_ticker.o
ccflags-y += -I$(srctree)/drivers/usb/gadget/

M:=$(shell pwd)

include $(M)/../env.mk

all: modules

clean: modules-clean
