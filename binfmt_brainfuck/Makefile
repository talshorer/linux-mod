obj-m := binfmt_brainfuck.o

EXECV=execv.out

M:=$(shell pwd)

include $(M)/../env.mk

all: $(EXECV) modules

clean: modules-clean bin-clean
