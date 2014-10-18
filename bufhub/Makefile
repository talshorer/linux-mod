obj-m := bufhub.o

TEST=test

THIS_MAKEFILE_DIR=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: $(TEST).out
	make -C $(KERNEL) M=$(PWD) modules

$(TEST).out: $(TEST).c
	$(CC) $< -o $@

clean:
	make -C $(KERNEL) M=$(PWD) clean
	rm -f $(TEST)
