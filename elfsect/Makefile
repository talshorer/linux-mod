obj-m := elfsect.o

# when elfsect_main.c is called section.c, the extra symbols from layout.lds
# fail to import
elfsect-y := elfsect_main.o
ldflags-y += -T$(M)/layout.lds

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: modules

clean: modules-clean
