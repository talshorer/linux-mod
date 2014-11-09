KERNEL=/lib/modules/$(shell uname -r)/build
LMOD_CFLAGS:=-Wall
ccflags-y+=$(LMOD_CFLAGS)

default: all

%.out: %.c
	$(CC) $< -o $@ $(LMOD_CFLAGS) $($@_CFLAGS)

lib%.so: %.c
	$(CC) $< -o $@ -shared $(LMOD_CFLAGS) $($@_CFLAGS)

kern_make:
	$(MAKE) -C $(KERNEL) M=$(M) $(KTARGET)

modules:
	$(MAKE) kern_make KTARGET=modules

modules-clean:
	$(MAKE) kern_make KTARGET=clean

gen-clean:
	rm -f *.gen.*

bin-clean:
	rm -f *.out
