#define MODULE_NAME "usblb_host"
#define pr_fmt(fmt) MODULE_NAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>

#include "usblb.h"

#define to_usblb_host(hcd) (*(struct usblb_host **)(hcd)->hcd_priv)

static struct class *usblb_host_class;

int usblb_host_init(void)
{
	usblb_host_class = class_create(THIS_MODULE, MODULE_NAME);
	if (IS_ERR(usblb_host_class)) {
		int err = PTR_ERR(usblb_host_class);
		pr_err("class_create failed. err = %d\n", err);
		return err;
	}
	return 0;
}

void usblb_host_exit(void)
{
	class_destroy(usblb_host_class);
}

static int usblb_host_start(struct usb_hcd *hcd)
{
	dev_info(to_usblb_host(hcd)->dev, "<%s>\n", __func__);
	return 0;
}

static void usblb_host_stop(struct usb_hcd *hcd)
{
	dev_info(to_usblb_host(hcd)->dev, "<%s>\n", __func__);
}

static int usblb_host_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		u16 wIndex, char *buf, u16 wLength)
{
	struct usblb_host *host = to_usblb_host(hcd);
	dev_info(host->dev, "<%s>\n", __func__);
	return 0;
}

static const struct hc_driver usblb_host_driver = {
	.description            = KBUILD_MODNAME "-hcd",
	.product_desc           = KBUILD_MODNAME " host driver",
	.hcd_priv_size          = sizeof(struct usblb_host *),
	.flags                  = HCD_USB2 | HCD_MEMORY,

	.start                  = usblb_host_start,
	.stop                   = usblb_host_stop,

#if 0 /* TODO */
	.get_frame_number       = usblb_host_get_frame_number,

	.urb_enqueue            = usblb_host_urb_enqueue,
	.urb_dequeue            = usblb_host_urb_dequeue,
	.endpoint_disable       = usblb_host_disable,

	.hub_status_data        = usblb_host_hub_status_data,
#endif /* 0 */
	.hub_control            = usblb_host_hub_control,
#if 0 /* TODO */
	.bus_suspend            = usblb_host_bus_suspend,
	.bus_resume             = usblb_host_bus_resume,
	/* .start_port_reset    = NULL, */
	/* .hub_irq_enable      = NULL, */
#endif /* 0 */
};

int usblb_host_device_setup(struct usblb_host *dev, int i)
{
	int err;

	dev->dev = device_create(usblb_host_class, NULL, MKDEV(0, i), dev,
			"%s%d", usblb_host_class->name, i);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	dev->hcd = usb_create_hcd(&usblb_host_driver,
			dev->dev, dev_name(dev->dev));
	if (!dev->hcd) {
		err = -ENOMEM;
		pr_err("usb_create_hcd failed for %s\n", dev_name(dev->dev));
		goto fail_usb_create_hcd;
	}
	*dev->hcd->hcd_priv = (unsigned long)dev;

	err = usb_add_hcd(dev->hcd, 0, 0);
	if (err) {
		pr_err("usb_add_hcd failed for %s\n", dev_name(dev->dev));
		goto fail_usb_add_hcd;
	}

	pr_info("created %s successfully\n", dev_name(dev->dev));
	return 0;

fail_usb_add_hcd:
	usb_put_hcd(dev->hcd);
fail_usb_create_hcd:
	device_destroy(usblb_host_class, dev->dev->devt);
fail_device_create:
	return err;
}

void usblb_host_device_cleanup(struct usblb_host *dev)
{
	pr_info("destroying %s\n", dev_name(dev->dev));
	usb_remove_hcd(dev->hcd);
	usb_put_hcd(dev->hcd);
	device_destroy(usblb_host_class, dev->dev->devt);
}

int usblb_host_set_gadget(struct usblb_host *h, struct usblb_gadget *g)
{
	int err;

	h->gadget = g;

	err = sysfs_create_link(&h->dev->kobj, &g->dev->kobj, "gadget");
	if (err) {
		pr_info("sysfs_create_link failed. i = %d, err = %d\n",
			MINOR(g->dev->devt), err);
		goto fail_sysfs_create_link;
	}

	return 0;

fail_sysfs_create_link:
	return err;
}
