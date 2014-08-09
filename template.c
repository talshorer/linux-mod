#include <linux/module.h>
#include <linux/kernel.h>

/* FARNAME stands for "find and replace NAME" */
static const char DRIVER_NAME[] = "FARNAME";

static int __init FARNAME_init(void)
{
	return 0;
}
module_init(FARNAME_init);

static void __exit FARNAME_exit(void)
{
}
module_exit(FARNAME_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("FIXME");
MODULE_VERSION("0.0");
MODULE_LICENSE("GPL");

