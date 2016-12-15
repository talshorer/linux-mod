#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>

struct usbtunnel {
	char *udc_name;
	struct usb_gadget_driver gadget_driver;
	struct usb_device *udev;
};

static int usbtunnel_host_probe(struct usb_device *udev)
{
	/* TODO */
	return -ENODEV;
}

static void usbtunnel_host_disconnect(struct usb_device *udev)
{
	/* TODO */
}

static ssize_t match_show(struct device_driver *driver, char *buf)
{
	/* TODO */
	return 0;
}

static ssize_t match_store(struct device_driver *driver, const char *buf,
		size_t count)
{
	/* TODO */
	return count;
}

static DRIVER_ATTR_RW(match);

static struct attribute *usbtunnel_host_driver_attrs[] = {
	&driver_attr_match.attr,
	NULL,
};
ATTRIBUTE_GROUPS(usbtunnel_host_driver);

static struct usb_device_driver usbtunnel_host_driver = {
	.name		= "usbtunnel-host",
	.probe		= usbtunnel_host_probe,
	.disconnect	= usbtunnel_host_disconnect,
	.supports_autosuspend	=	0,
	.drvwrap.driver.groups = usbtunnel_host_driver_groups,
};

static int __init usbtunnel_init(void)
{
	int err;

	err = usb_register_device_driver(&usbtunnel_host_driver, THIS_MODULE);
	if (err)
		goto fail_usb_register_device_driver;
	return 0;

fail_usb_register_device_driver:
	return err;
}
module_init(usbtunnel_init);

static void __exit usbtunnel_exit(void)
{
	usb_deregister_device_driver(&usbtunnel_host_driver);
}
module_exit(usbtunnel_exit);


MODULE_AUTHOR();
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("usb tunnel: export a connected device to a connected host");
MODULE_VERSION("0.0.1");
