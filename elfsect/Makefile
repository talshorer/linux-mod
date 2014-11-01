obj-m := elfsect.o

DUMMIES_GENERATOR := gen_dummies_source.sh

M:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(M)/../env.mk

# important note from elfsect 1.0.* (irrelevant since 1.1.0):
#	when elfsect_main.c is called elfsect.c, the extra symbols from
#	layout.lds fail to import
elfsect-y := elfsect_main.o $(shell $(M)/$(DUMMIES_GENERATOR) -l)
ldflags-y += -T$(M)/layout.lds

dummies: $(DUMMIES_GENERATOR)
	./$< -c

all: dummies modules

clean: modules-clean gen-clean
