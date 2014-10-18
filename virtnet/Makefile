VIRTNET_BACKENDS := virtnet_lb.o virtnet_chr.o
obj-m := virtnet.o
virtnet-y := virtnet_net.o virtnet_backend_glue.o $(VIRTNET_BACKENDS)

BACKEND_GLUE_HEADER := virtnet_backend_glue.h.gen

THIS_MAKEFILE_DIR=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: backend_glue_header
	make -C $(KERNEL) M=$(PWD) modules

backend_glue_header:
	bash gen_backend_glue_header.sh $(VIRTNET_BACKENDS) > \
			$(BACKEND_GLUE_HEADER)

clean:
	make -C $(KERNEL) M=$(PWD) clean
	rm -f $(BACKEND_GLUE_HEADER)
