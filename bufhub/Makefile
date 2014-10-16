obj-m := bufhub.o

TEST=test

include $(PWD)/../env.mk

all: $(TEST).out
	make -C $(KERNEL) M=$(PWD) modules

$(TEST).out: $(TEST).c
	$(CC) $< -o $@

clean:
	make -C $(KERNEL) M=$(PWD) clean
	rm -f $(TEST)
