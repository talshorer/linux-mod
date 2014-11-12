#include <linux/module.h>
#include <linux/kernel.h>

#include "exporter.h"

#define MODULE_NAME "importer"

static int __init importer_init(void)
{
	printk(KERN_INFO "%s: invoking exported symbol %pf\n", MODULE_NAME,
			exporter_fn);
	exporter_fn();
	return 0;
}
module_init(importer_init);

static void __exit importer_exit(void)
{
}
module_exit(importer_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A module that uses a symbol exported by another module");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
