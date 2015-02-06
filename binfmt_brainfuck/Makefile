obj-m := binfmt_brainfuck.o

EXE := execv.out bf.out

M:=$(shell pwd)

include $(M)/../env.mk

all: $(EXE) modules

clean: modules-clean bin-clean
