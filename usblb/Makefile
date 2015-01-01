obj-m := usblb.o
usblb-y := usblb_gadget.o usblb_host.o usblb_main.o usblb_glue.o

TEST=test.pyc

M:=$(shell pwd)

include $(M)/../env.mk

all: $(TEST) modules

clean: modules-clean py-clean
