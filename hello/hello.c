#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <lmod/meta.h>

int init_module(void)
{
	pr_info("hello, world!\n");
	return 0;
}

void cleanup_module(void)
{
	pr_info("goodbye, world!\n");
}

LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A simple module that prints to log upon init and exit");
MODULE_VERSION("1.0.2");
