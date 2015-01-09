#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

static int __init ticker_init(void)
{
	return 0;
}
module_init(ticker_init);

static void __exit ticker_exit(void)
{
}
module_exit(ticker_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("USB ticker host device driver");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");
