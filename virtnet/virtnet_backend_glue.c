#define pr_fmt(fmt) KBUILD_BASENAME ": " fmt

#include "virtnet.h"

struct virtnet_backend_entry {
	char *name;
	struct virtnet_backend_ops *ops;
};

/*
 * this is where the magic happens. virtnet_backend_glue.gen.h is a generated
 * file that declares an extern struct virtnet_backend_ops for each backend in
 * the Makefile. it also declares a macro called VIRTNET_BACKEND_GLUE, which
 * expands to call VIRTNET_BACKEND_ENTRY for each backend, separated by commas.
 * by doing this, adding a new backend is done only in the Makefile, and
 * everything else adapts to it. it's important to note that each backend must
 * define a non-static struct virtnet_backend_ops called ${backend}_backend_ops
 */

#define VIRTNET_BACKEND_ENTRY(_name)       \
{                                          \
	.name = #_name,                        \
	.ops = &virtnet_##_name##_backend_ops, \
}

static struct virtnet_backend_entry virtnet_backends[] = {
	VIRTNET_BACKEND_GLUE()
};

struct virtnet_backend_ops *virtnet_get_backend(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(virtnet_backends); i++)
		if (!strcmp(name, virtnet_backends[i].name))
			return virtnet_backends[i].ops;
	pr_err("unknown backend %s\n", name);
	return NULL;
}
