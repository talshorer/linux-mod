#include "virtnet_backend_glue.h.gen"

#define MODULE_NAME "virtnet_backend"

struct virtnet_backend_entry {
	char *name;
	struct virtnet_backend_ops *ops;
};

/*
 * this is where the magic happens. virtnet_backend_glue.h.gen is a generated
 * file that declares an extern struct virtnet_backend_ops for each backend in
 * the Makefile. it also declares a macro called VIRTNET_BACKEND_GLUE, which
 * expands to call VIRTNET_BACKEND_ENTRY for each backend, seperated by commas.
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
	printk(KERN_ERR "%s: unknown backend %s\n", MODULE_NAME, name);
	return NULL;
}