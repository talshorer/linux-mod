#include <linux/module.h>
#include <linux/kernel.h>

#define MODULE_NAME "exporter"

void exporter_fn(void)
{
	printk(KERN_INFO "%s: <%s> was invoked\n", MODULE_NAME, __func__);
}
EXPORT_SYMBOL_GPL(exporter_fn);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A module that exports a function");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
