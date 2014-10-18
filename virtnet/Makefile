VIRTNET_BACKENDS := virtnet_lb.o virtnet_chr.o
obj-m := virtnet.o
virtnet-y := virtnet_net.o virtnet_backend_glue.o $(VIRTNET_BACKENDS)
BACKEND_GLUE_HEADER := virtnet_backend_glue.h.gen

THIS_MAKEFILE_DIR:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(THIS_MAKEFILE_DIR)/../env.mk

all: $(BACKEND_GLUE_HEADER)
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) modules

$(BACKEND_GLUE_HEADER): gen_backend_glue_header.sh
	bash $< $(VIRTNET_BACKENDS) > $@

clean:
	make -C $(KERNEL) M=$(THIS_MAKEFILE_DIR) clean
	rm -f $(BACKEND_GLUE_HEADER)
