#include <linux/module.h>
#include <linux/kernel.h>

#include "usblb.h"

int usblb_gadget_init(void)
{
	return 0;
}

void usblb_gadget_exit(void)
{
}

int usblb_gadget_device_setup(struct usblb_gadget *dev, int i)
{
	return 0;
}

void usblb_gadget_device_cleanup(struct usblb_gadget *dev)
{
}

int usblb_gadget_set_host(struct usblb_gadget *g, struct usblb_host *h)
{
	g->host = h;
	return 0;
}
