KERNEL=/lib/modules/$(shell uname -r)/build

%.out: %.c
	$(CC) $< -o $@
