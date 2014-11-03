KERNEL=/lib/modules/$(shell uname -r)/build
LMOD_CFLAGS:=-Wall
ccflags-y+=$(LMOD_CFLAGS)

default: all

%.out: %.c
	$(CC) $< -o $@ $(LMOD_CFLAGS) $($@_CFLAGS)

lib%.so: %.c
	$(CC) $< -o $@ -shared $(LMOD_CFLAGS) $($@_CFLAGS)

kern_make:
	make -C $(KERNEL) M=$(M) $(KTARGET)

modules:
	make kern_make KTARGET=modules

modules-clean:
	make kern_make KTARGET=clean

gen-clean:
	rm -f *.gen.*

bin-clean:
	rm -f *.out
