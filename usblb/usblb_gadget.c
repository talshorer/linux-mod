#define MODULE_NAME "usblb_gadget"
#define pr_fmt(fmt) MODULE_NAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>

#include "usblb.h"

static struct class *usblb_gadget_class;

int usblb_gadget_init(void)
{
	usblb_gadget_class = class_create(THIS_MODULE, MODULE_NAME);
	if (IS_ERR(usblb_gadget_class)) {
		int err = PTR_ERR(usblb_gadget_class);
		pr_err("class_create failed. err = %d\n", err);
		return err;
	}
	return 0;
}

void usblb_gadget_exit(void)
{
	class_destroy(usblb_gadget_class);
}

int usblb_gadget_device_setup(struct usblb_gadget *dev, int i)
{
	int err;

	dev->dev = device_create(usblb_gadget_class, NULL, MKDEV(0, i), dev,
			"%s%d", usblb_gadget_class->name, i);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	pr_info("created %s successfully\n", dev_name(dev->dev));
	return 0;

fail_device_create:
	return err;
}

void usblb_gadget_device_cleanup(struct usblb_gadget *dev)
{
	pr_info("destroying %s\n", dev_name(dev->dev));
	device_destroy(usblb_gadget_class, dev->dev->devt);
}

int usblb_gadget_set_host(struct usblb_gadget *g, struct usblb_host *h)
{
	int err;

	g->host = h;

	err = sysfs_create_link(&g->dev->kobj, &h->dev->kobj, "host");
	if (err) {
		pr_info("sysfs_create_link failed. i = %d, err = %d\n",
			MINOR(g->dev->devt), err);
		goto fail_sysfs_create_link;
	}

	return 0;

fail_sysfs_create_link:
	return err;
}
