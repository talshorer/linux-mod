VIRTNET_BACKENDS := virtnet_lb.o virtnet_chr.o
obj-m := virtnet.o
virtnet-y := virtnet_net.o virtnet_backend_glue.o $(VIRTNET_BACKENDS)
BACKEND_GLUE_HEADER := virtnet_backend_glue.gen.h

TEST=test.out

M:=$(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

include $(M)/../env.mk

all: $(TEST) $(BACKEND_GLUE_HEADER) modules

$(BACKEND_GLUE_HEADER): gen_backend_glue_header.sh
	bash $< $(VIRTNET_BACKENDS) > $@

clean: modules-clean gen-clean bin-clean
