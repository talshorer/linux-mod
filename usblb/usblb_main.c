#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include "usblb.h"

static int usblb_ndevices = 1;
module_param_named(ndevices, usblb_ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "number of usblb devices to create");

static int __init usblb_check_module_params(void) {
	int err = 0;
	if (usblb_ndevices <= 0) {
		pr_err("usblb_ndevices <= 0. value = %d\n", usblb_ndevices);
		err = -EINVAL;
	}
	return err;
}

struct usblb_device {
	struct usblb_gadget gadget;
	struct usblb_host host;
};

static struct usblb_device *usblb_devices;

int usblb_aquire_bus(struct usblb_device *dev)
{
	/* TODO */
	return -ENOLCK;
}

int usblb_aquire_bus_g(struct usblb_gadget *g)
{
	return usblb_aquire_bus(container_of(g, struct usblb_device, gadget));
}

int usblb_aquire_bus_h(struct usblb_host *h)
{
	return usblb_aquire_bus(container_of(h, struct usblb_device, host));
}

static int usblb_device_setup(struct usblb_device *dev, int i)
{
	int err;

	err = usblb_gadget_device_setup(&dev->gadget, i);
	if (err)
		goto fail_g_setup;

	err = usblb_host_device_setup(&dev->host, i);
	if (err)
		goto fail_h_setup;

	/* both of these should be undone by usblb_*_device_cleanup */
	err = usblb_gadget_set_host(&dev->gadget, &dev->host) ||
			usblb_host_set_gadget(&dev->host, &dev->gadget);
	if (err)
		goto fail_attach;

	pr_info("created %s%d successfully\n", KBUILD_MODNAME, i);
	return 0;

fail_attach:
	usblb_host_device_cleanup(&dev->host);
fail_h_setup:
	usblb_gadget_device_cleanup(&dev->gadget);
fail_g_setup:
	return err;
}

static void usblb_device_cleanup(struct usblb_device *dev)
{
	pr_info("destroying %s%d\n",
			KBUILD_MODNAME, (int)(dev - usblb_devices));
	usblb_host_device_cleanup(&dev->host);
	usblb_gadget_device_cleanup(&dev->gadget);
}

static int __init usblb_init(void)
{
	int err;
	int i;

	err = usblb_check_module_params();
	if (err)
		return err;

	err = usblb_gadget_init();
	if (err)
		goto fail_usblb_gadget_init;
	err = usblb_host_init();
	if (err)
		goto fail_usblb_host_init;

	usblb_devices = vmalloc(sizeof(usblb_devices[0]) * usblb_ndevices);
	if (!usblb_devices) {
		err = -ENOMEM;
		pr_err("failed to allocate usblb_devices\n");
		goto fail_vmalloc_usblb_devices;
	}
	memset(usblb_devices, 0, sizeof(usblb_devices[0]) * usblb_ndevices);

	for (i = 0; i < usblb_ndevices; i++) {
		err = usblb_device_setup(&usblb_devices[i], i);
		if (err) {
			pr_err("usblb_device_setup failed. i = %d, err = %d\n",
					i, err);
			goto fail_usblb_device_setup_loop;
		}
	}

	pr_info("initialized successfully\n");
	return 0;

fail_usblb_device_setup_loop:
	while (i--)
		usblb_device_cleanup(&usblb_devices[i]);
	vfree(usblb_devices);
fail_vmalloc_usblb_devices:
	usblb_host_exit();
fail_usblb_host_init:
	usblb_gadget_exit();
fail_usblb_gadget_init:
	return err;
}
module_init(usblb_init);

static void __exit usblb_exit(void)
{
	int i;
	for (i = 0; i < usblb_ndevices; i++)
		usblb_device_cleanup(&usblb_devices[i]);
	vfree(usblb_devices);
	usblb_host_exit();
	usblb_gadget_exit();
	pr_info("exited successfully\n");
}
module_exit(usblb_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Loopback between virtual usb gadget and host controllers");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");
