KERNEL ?= /lib/modules/$(shell uname -r)/build
LMOD_TOP := $(dir $(lastword $(MAKEFILE_LIST)))

LMOD_CFLAGS += -Wall -Werror -D_GNU_SOURCE -I$(LMOD_TOP)/utils/include
LMOD_US_CFLAGS += $(LMOD_CFLAGS) $(TARGET_CFLAGS)
ccflags-y += $(LMOD_CFLAGS)

CHECKFLAGS += -Wsparse-all -Wsparse-error -Wno-shadow

CC = $(CROSS_COMPILE)gcc
CLEAN = rm -f

default: all

%.out: %.c
	@echo "  BUILD $@"
	@$(CC) $< -o $@ $(LMOD_US_CFLAGS) $($@_CFLAGS)

%.pyc: %.py
	pycompile $<

define kern_make
$(MAKE) -C $(KERNEL) M=$(M) $1 C=1
endef

modules:
	$(call kern_make, modules)

modules-clean:
	$(call kern_make, clean)

gen-clean:
	$(CLEAN) *.gen.*

bin-clean:
	$(CLEAN) *.out

py-clean:
	$(CLEAN) *.pyc

backup-clean:
	$(CLEAN) *~

# always clean backup files
clean: backup-clean
