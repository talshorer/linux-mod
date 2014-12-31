#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include "usblb.h"

static int usblb_nbuses = 1;
module_param_named(nbuses, usblb_nbuses, int, 0);
MODULE_PARM_DESC(nbuses, "number of usblb buses to create");

static int __init usblb_check_module_params(void) {
	int err = 0;
	if (usblb_nbuses <= 0) {
		pr_err("usblb_nbuses <= 0. value = %d\n", usblb_nbuses);
		err = -EINVAL;
	}
	return err;
}

static struct usblb_bus *usblb_buses;

static int usblb_bus_setup(struct usblb_bus *bus, int i)
{
	int err;

	spin_lock_init(&bus->lock);
	bus->busnum = i;
	atomic_set(&bus->event, 0);
	atomic_set(&bus->in_transfer, 0);

	err = usblb_gadget_device_setup(&bus->gadget, i);
	if (err)
		goto fail_g_setup;

	err = usblb_host_device_setup(&bus->host, i);
	if (err)
		goto fail_h_setup;

	/* both of these should be undone by usblb_*_device_cleanup */
	err = usblb_gadget_set_host(&bus->gadget, &bus->host) ||
			usblb_host_set_gadget(&bus->host, &bus->gadget);
	if (err)
		goto fail_attach;

	atomic_set(&bus->transfer_active, 1);
	setup_timer(&bus->transfer_timer, usblb_glue_transfer_timer_func,
			(unsigned long)bus);
	mod_timer(
		&bus->transfer_timer,
		jiffies + USBLB_TRANSFER_INTERVAL_JIFFIES
	);

	pr_info("created %s%d successfully\n", KBUILD_MODNAME, i);
	return 0;

fail_attach:
	usblb_host_device_cleanup(&bus->host);
fail_h_setup:
	usblb_gadget_device_cleanup(&bus->gadget);
fail_g_setup:
	return err;
}

static void usblb_bus_cleanup(struct usblb_bus *bus)
{
	unsigned long flags;
	pr_info("destroying %s%d\n",
			KBUILD_MODNAME, (int)(bus - usblb_buses));
	usblb_bus_lock_irqsave(bus, flags);
	atomic_set(&bus->transfer_active, 0);
	usblb_bus_unlock_irqrestore(bus, flags);
	del_timer_sync(&bus->transfer_timer);
	usblb_glue_cleanup_queues(bus);
	usblb_host_device_cleanup(&bus->host);
	usblb_gadget_device_cleanup(&bus->gadget);
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

	usblb_buses = vmalloc(sizeof(usblb_buses[0]) * usblb_nbuses);
	if (!usblb_buses) {
		err = -ENOMEM;
		pr_err("failed to allocate usblb_buses\n");
		goto fail_vmalloc_usblb_buses;
	}
	memset(usblb_buses, 0, sizeof(usblb_buses[0]) * usblb_nbuses);

	for (i = 0; i < usblb_nbuses; i++) {
		err = usblb_bus_setup(&usblb_buses[i], i);
		if (err) {
			pr_err("usblb_bus_setup failed. i = %d, err = %d\n",
					i, err);
			goto fail_usblb_bus_setup_loop;
		}
	}

	pr_info("initialized successfully\n");
	return 0;

fail_usblb_bus_setup_loop:
	while (i--)
		usblb_bus_cleanup(&usblb_buses[i]);
	vfree(usblb_buses);
fail_vmalloc_usblb_buses:
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
	for (i = 0; i < usblb_nbuses; i++)
		usblb_bus_cleanup(&usblb_buses[i]);
	vfree(usblb_buses);
	usblb_host_exit();
	usblb_gadget_exit();
	pr_info("exited successfully\n");
}
module_exit(usblb_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Loopback between virtual usb gadget and host controllers");
MODULE_VERSION("0.4.0");
MODULE_LICENSE("GPL");
