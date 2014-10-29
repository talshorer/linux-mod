obj-m := elfsect.o

# important note from elfsect 1.0.* (irrelevant since 1.1.0):
#	when elfsect_main.c is called elfsect.c, the extra symbols from
#	layout.lds fail to import
elfsect-y := elfsect_main.o elfsect_foo.o elfsect_bar.o
ldflags-y += -T$(M)/layout.lds

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: modules

clean: modules-clean
