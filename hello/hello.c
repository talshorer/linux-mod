/*
 *  hello.c - The simplest kernel module.
 */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */

#include <lmod/meta.h>

int init_module(void)
{
	pr_info("Hello, World!\n");

	/*
	 * A non 0 return means init_module failed; module can't be loaded.
	 */
	return 0;
}

void cleanup_module(void)
{
	pr_info("Goodbye, World!\n");
}

LMOD_MODULE_META();
MODULE_DESCRIPTION("A simple module that prints to log upon init and exit");
MODULE_VERSION("1.0.1");
