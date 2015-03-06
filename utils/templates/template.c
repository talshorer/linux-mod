#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <lmod/meta.h>

static int __init __FILE_NAME_LOWERCASE_init(void)
{
	return 0;
}
module_init(__FILE_NAME_LOWERCASE_init);

static void __exit __FILE_NAME_LOWERCASE_exit(void)
{
}
module_exit(__FILE_NAME_LOWERCASE_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("FIXME");
MODULE_VERSION("0.0.1");
