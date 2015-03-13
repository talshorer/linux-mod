#include <linux/module.h>
#include <linux/kernel.h>

#include <lmod/meta.h>

#define MODULE_NAME "exporter"

void exporter_fn(void)
{
	pr_info("%s: <%s> was invoked\n", MODULE_NAME, __func__);
}
EXPORT_SYMBOL_GPL(exporter_fn);


LMOD_MODULE_META();
MODULE_DESCRIPTION("A module that exports a function");
MODULE_VERSION("1.0.1");
