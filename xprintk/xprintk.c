#include <linux/module.h>
#include <linux/kernel.h>

#include <lmod/meta.h>

#include "xprintk.h"

#define MODULE_NAME "xprintk"

char *x_name(struct xprintk_x *x)
{
	return MODULE_NAME;
}

static struct xprintk_x xprintk_x;

#define xprintk_msg(level) x_##level(&xprintk_x, "%s\n", #level)

static int __init xprintk_init(void)
{
	xprintk_msg(debug);
	xprintk_msg(info);
	xprintk_msg(notice);
	xprintk_msg(warn);
	xprintk_msg(err);
	xprintk_msg(crit);
	xprintk_msg(alert);
	xprintk_msg(emerg);
	return 0;
}
module_init(xprintk_init);

static void __exit xprintk_exit(void)
{
}
module_exit(xprintk_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("Subsystem-specific printk-like functions");
MODULE_VERSION("1.0.0");
