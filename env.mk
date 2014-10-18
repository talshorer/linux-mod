KERNEL=/lib/modules/$(shell uname -r)/build

default: all

%.out: %.c
	$(CC) $< -o $@

kern_make:
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) $(KERN_RULE)

modules:
	make kern_make KERN_RULE=modules

modules-clean:
	make kern_make KERN_RULE=clean
