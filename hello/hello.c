/*  
 *  hello.c - The simplest kernel module.
 */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */

int init_module(void)
{
	printk(KERN_INFO "Hello, World!\n");

	/* 
	 * A non 0 return means init_module failed; module can't be loaded. 
	 */
	return 0;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Goodbye, World!\n");
}

MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A simple module that prints to log upon init and exit");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

