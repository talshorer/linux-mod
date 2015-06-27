KERNEL ?= /lib/modules/$(shell uname -r)/build
LMOD_TOP := $(dir $(lastword $(MAKEFILE_LIST)))

LMOD_CFLAGS += -Wall -Werror -D_GNU_SOURCE -I$(LMOD_TOP)/utils/include
LMOD_US_CFLAGS += $(LMOD_CFLAGS) $(TARGET_CFLAGS)
ccflags-y += $(LMOD_CFLAGS)

CHECKFLAGS += -Wsparse-all -Wsparse-error -Wno-shadow

CC = $(CROSS_COMPILE)gcc

default: all

%.out: %.c
	$(CC) $< -o $@ $(LMOD_US_CFLAGS) $($@_CFLAGS)

lib%.so: %.c
	$(CC) $< -o $@ -shared $(LMOD_US_CFLAGS) $($@_CFLAGS)

%.pyc: %.py
	pycompile $<

kern_make:
	$(MAKE) -C $(KERNEL) M=$(M) $(KTARGET) C=1

modules:
	$(MAKE) kern_make KTARGET=modules

modules-clean:
	$(MAKE) kern_make KTARGET=clean

gen-clean:
	rm -f *.gen.*

bin-clean:
	rm -f *.out

py-clean:
	rm -f *.pyc

backup-clean:
	rm -f *~

# always clean backup files
clean: backup-clean
