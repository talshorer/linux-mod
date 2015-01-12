#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "usbticker.h"

struct usbticker {
	struct usb_device *udev;
	atomic_t ticks;
	__le32 buf;
	struct urb *urb;
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
	pr_debug("<%s>\n", __func__);
	return sprintf(buf, "%u\n", atomic_read(&ticker->ticks));
}

static DEVICE_ATTR_RO(ticks);

static void ticker_interrupt_callback(struct urb *urb)
{
	struct usbticker *ticker = urb->context;
	int status;

	pr_debug("<%s>\n", __func__);

	switch (urb->status) {
	case 0: /* success */
		break;
	case -ECONNRESET: /* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPROTO:
		return;
	/* -EPIPE:  should clear the halt */
	default: /* error */
		goto resubmit;
	}

	atomic_set(&ticker->ticks, (int)le32_to_cpu(ticker->buf));

resubmit:
	//return;
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		pr_err("<%s> usb_submit_urb failed, status = %d\n",
				__func__, status);
}

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

	ticker->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ticker->urb) {
		err = -ENOMEM;
		pr_err("<%s> usb_alloc_urb failed\n", __func__);
		goto fail_usb_alloc_urb;
	}
	usb_fill_int_urb(ticker->urb, udev, usb_rcvintpipe(udev, 1),
			&ticker->buf, sizeof(ticker->buf),
			ticker_interrupt_callback,
			ticker, TICKER_POLL_INTERVAL_MS);

	err = usb_submit_urb(ticker->urb, GFP_KERNEL);
	if (err) {
		pr_err("<%s> usb_submit_urb failed, err = %d\n",
				__func__, err);
		goto fail_usb_alloc_urb;
	}

	return 0;

fail_usb_alloc_urb:
	device_remove_file(&interface->dev, &dev_attr_ticks);
fail_device_create_file_ticks:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(udev);
	kfree(ticker);
fail_kzalloc_ticker:
	return err;
}

static void ticker_disconnect(struct usb_interface *interface)
{
	struct usbticker *ticker = usb_get_intfdata(interface);
	pr_debug("<%s>\n", __func__);
	usb_kill_urb(ticker->urb);
	usb_free_urb(ticker->urb);
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
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
