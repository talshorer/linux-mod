#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "usbticker_defs.h"

struct usbticker {
	struct usb_device *udev;
	atomic_t ticks;
};

static const struct usb_device_id ticker_id_table[] = {
	{ USB_DEVICE(usbticker_idVendor, usbticker_idProduct) },
	{ },
};
MODULE_DEVICE_TABLE(usb, ticker_id_table);

static ssize_t ticks_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usbticker *ticker;
	ticker = usb_get_intfdata(to_usb_interface(dev));
	return sprintf(buf, "%u\n", atomic_read(&ticker->ticks));
}

static DEVICE_ATTR_RO(ticks);

static int ticker_probe(struct usb_interface *interface,
		const struct usb_device_id *id)
{
	struct usbticker *ticker;
	struct usb_device *udev = interface_to_usbdev(interface);
	int err;

	pr_debug("<%s>\n", __func__);

	ticker = kzalloc(sizeof(*ticker), GFP_KERNEL);
	if (!ticker) {
		err = -ENOMEM;
		pr_err("<%s> failed to allocate ticker\n", __func__);
		goto fail_kzalloc_ticker;
	}

	ticker->udev = usb_get_dev(udev);
	usb_set_intfdata(interface, ticker);

	err = device_create_file(&interface->dev, &dev_attr_ticks);
	if (err) {
		pr_err("<%s> failed to create ticks sysfs file, err = %d\n",
				__func__, err);
		goto fail_device_create_file_ticks;
	}

	return 0;

fail_device_create_file_ticks:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(udev);
	kfree(ticker);
fail_kzalloc_ticker:
	return err;
}

static void ticker_disconnect(struct usb_interface *interface)
{
	struct usbticker *ticker;
	pr_debug("<%s>\n", __func__);
	ticker = usb_get_intfdata(interface);
	device_remove_file(&interface->dev, &dev_attr_ticks);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(ticker->udev);
	kfree(ticker);
}

static struct usb_driver ticker_driver = {
	.name =       KBUILD_MODNAME,
	.probe =      ticker_probe,
	.disconnect = ticker_disconnect,
	.id_table =   ticker_id_table,
};

module_usb_driver(ticker_driver);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("USB ticker host device driver");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");
