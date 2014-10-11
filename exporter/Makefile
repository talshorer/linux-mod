obj-m := exporter.o importer.o

include $(PWD)/../env.mk

all:
	make -C $(KERNEL) M=$(PWD) modules

clean:
	make -C $(KERNEL) M=$(PWD) clean
