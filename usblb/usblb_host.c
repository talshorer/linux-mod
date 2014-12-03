#include <linux/module.h>
#include <linux/kernel.h>

#include "usblb.h"

int usblb_host_init(void)
{
	return 0;
}

void usblb_host_exit(void)
{
}

int usblb_host_device_setup(struct usblb_host *dev, int i)
{
	return 0;
}

void usblb_host_device_cleanup(struct usblb_host *dev)
{
}

int usblb_host_set_gadget(struct usblb_host *h, struct usblb_gadget *g)
{
	h->gadget = g;
	return 0;
}
