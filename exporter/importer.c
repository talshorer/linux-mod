#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <lmod/meta.h>

#include "exporter.h"

static int __init importer_init(void)
{
	pr_info("invoking exported symbol %pf\n", exporter_fn);
	exporter_fn();
	return 0;
}
module_init(importer_init);

static void __exit importer_exit(void)
{
}
module_exit(importer_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A module that uses a symbol exported by another module");
MODULE_VERSION("1.0.2");
