KERNEL=/lib/modules/$(shell uname -r)/build
LMOD_CFLAGS:=-Wall

default: all

%.out: %.c
	$(CC) $< -o $@ $(LMOD_CFLAGS) $($@_CFLAGS)

lib%.so: %.c
	$(CC) $< -o $@ -shared $(LMOD_CFLAGS) $($@_CFLAGS)

kern_make:
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) $(KERN_RULE)

modules:
	make kern_make KERN_RULE=modules

modules-clean:
	make kern_make KERN_RULE=clean
