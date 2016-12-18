#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>

#define MAX_DEVNAME_LEN 32

struct usbtunnel {
	char udc[MAX_DEVNAME_LEN + 1];
	char port[MAX_DEVNAME_LEN + 1];
	struct usb_gadget_driver gadget_driver;
	struct usb_device *udev;
	struct list_head list;
};

static LIST_HEAD(usbtunnel_list);
static DEFINE_SPINLOCK(usbtunnel_list_lock);

static struct usbtunnel *usbtunnel_lookup(const char *port)
{
	struct usbtunnel *ret;
	unsigned long flags;

	spin_lock_irqsave(&usbtunnel_list_lock, flags);
	list_for_each_entry(ret, &usbtunnel_list, list)
		if (!strcmp(port, ret->port))
			goto found;
	ret = NULL;
found:
	spin_unlock_irqrestore(&usbtunnel_list_lock, flags);
	return ret;
}

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
	size_t ret = 0;
	unsigned long flags;
	struct usbtunnel *ut;

	spin_lock_irqsave(&usbtunnel_list_lock, flags);
	list_for_each_entry(ut, &usbtunnel_list, list)
		ret += snprintf(&buf[ret], PAGE_SIZE - ret, "%s %s\n",
			ut->port, ut->udc);
	spin_unlock_irqrestore(&usbtunnel_list_lock, flags);

	return ret;
}

static int usbtunnel_gadget_bind(struct usb_gadget *gadget,
		struct usb_gadget_driver *gdriver)
{
	/* TODO */
	return -ENODEV;
}

static void usbtunnel_gadget_unbind(struct usb_gadget *gadget)
{
	/* TODO */
}

static int usbtunnel_gadget_setup(struct usb_gadget *gadget,
		const struct usb_ctrlrequest *ctrl)
{
	/* TODO */
	return -EINVAL;
}

static void usbtunnel_gadget_disconnect(struct usb_gadget *gadget)
{
	/* TODO */
}

static void usbtunnel_gadget_suspend(struct usb_gadget *gadget)
{
	/* TODO */
}

static void usbtunnel_gadget_resume(struct usb_gadget *gadget)
{
	/* TODO */
}

static const struct usb_gadget_driver usbtunnel_gadget_driver_template = {
	.bind       = usbtunnel_gadget_bind,
	.unbind     = usbtunnel_gadget_unbind,

	.setup      = usbtunnel_gadget_setup,
	.reset      = usbtunnel_gadget_disconnect,
	.disconnect = usbtunnel_gadget_disconnect,

	.suspend    = usbtunnel_gadget_suspend,
	.resume     = usbtunnel_gadget_resume,

	/* TODO function? */
	/* TODO max_speed? */

	.driver	= {
		/* TODO name? */
		.owner = THIS_MODULE,
	},
};

static int usbtunnel_add(const char *buf, size_t len)
{
	const char *sep;
	size_t portlen;
	size_t udclen;
	struct usbtunnel *ut;
	unsigned long flags;

	if (buf[len - 1] == '\n')
		len--;
	sep = memchr(buf, ' ', len);
	if (!sep)
		return -EINVAL;
	portlen = sep - buf;
	udclen = len - 1 - portlen;
	if (portlen > MAX_DEVNAME_LEN || udclen > MAX_DEVNAME_LEN)
		return -EINVAL;
	sep++;
	ut = kzalloc(sizeof(*ut), GFP_KERNEL);
	if (!ut)
		return -ENOMEM;

	memcpy(ut->port, buf, portlen);
	memcpy(ut->udc, sep, udclen);
	pr_info("tunneling port %s to udc %s\n", ut->port, ut->udc);
	ut->gadget_driver = usbtunnel_gadget_driver_template;
	/* TODO more fields. udc? */
	/* TODO probe */

	spin_lock_irqsave(&usbtunnel_list_lock, flags);
	list_add(&ut->list, &usbtunnel_list);
	spin_unlock_irqrestore(&usbtunnel_list_lock, flags);

	return 0;
}

static void usbtunnel_cleanup(struct usbtunnel *ut)
{
	unsigned long flags;

	spin_lock_irqsave(&usbtunnel_list_lock, flags);
	list_del(&ut->list);
	spin_unlock_irqrestore(&usbtunnel_list_lock, flags);

	pr_info("removed tunnel on port %s\n", ut->port);
	kfree(ut);
}

static int usbtunnel_del(const char *buf, size_t len)
{
	char *s;
	struct usbtunnel *ut;

	if (buf[len - 1] == '\n')
		len--;
	s = kmemdup(buf, len + 1, GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s[len] = '\0';
	ut = usbtunnel_lookup(s);
	kfree(s);
	if (!ut)
		return -EINVAL;
	usbtunnel_cleanup(ut);
	return 0;
}

static ssize_t match_store(struct device_driver *driver, const char *buf,
		size_t count)
{
	/*
	 * one of:
	 * ... "add PORT UDC\n"
	 * ... "del PORT\n"
	 * the '\n' is optional.
	 */
	int err;

	if (!strncmp(buf, "add ", 4))
		err = usbtunnel_add(buf + 4, count - 4) ?: count;
	else if (!strncmp(buf, "del ", 4))
		err = usbtunnel_del(buf + 4, count - 4) ?: count;
	else
		err = -EINVAL;

	return err ?: count;
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
	struct usbtunnel *ut;
	struct usbtunnel *tmp;
	unsigned long flags;

	usb_deregister_device_driver(&usbtunnel_host_driver);
	spin_lock_irqsave(&usbtunnel_list_lock, flags);
	list_for_each_entry_safe(ut, tmp, &usbtunnel_list, list) {
		spin_unlock_irqrestore(&usbtunnel_list_lock, flags);
		usbtunnel_cleanup(ut);
		spin_lock_irqsave(&usbtunnel_list_lock, flags);
	}
	spin_unlock_irqrestore(&usbtunnel_list_lock, flags);
}
module_exit(usbtunnel_exit);


MODULE_AUTHOR();
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("usb tunnel: export a connected device to a connected host");
MODULE_VERSION("0.0.1");
