#include <linux/module.h>
#include <linux/kernel.h>

#define MODULE_NAME "__MODULE_NAME_LOWERCASE"

static int __init __FILE_NAME_LOWERCASE_init(void)
{
	return 0;
}
module_init(__FILE_NAME_LOWERCASE_init);

static void __exit __FILE_NAME_LOWERCASE_exit(void)
{
}
module_exit(__FILE_NAME_LOWERCASE_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("FIXME");
MODULE_VERSION("0.0.0");
MODULE_LICENSE("GPL");
