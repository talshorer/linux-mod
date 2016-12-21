#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>

#include "u_f.h"

#define MAX_DEVNAME_LEN 32
#define USBTUNNEL_EP0_BUFSIZ 1024
/* seems reasonable. we can't allow 5000 because then our host will timeout */
#define USBTUNNEL_CONTROL_MSG_TIMEOUT 2006

struct usbtunnel {
	char udc[MAX_DEVNAME_LEN + 1];
	char port[MAX_DEVNAME_LEN + 1];
	struct list_head list;
	struct {
		char function[10 + MAX_DEVNAME_LEN + 1]; /* usbtunnel.$PORT\0 */
		struct usb_gadget_driver driver;
		struct usb_gadget *gadget;
		struct usb_request *ep0_req;
		struct {
			__u8 requesttype;
			__u8 request;
			__u16 value;
			__u16 index;
			__u16 length;
			/*
			 * state:
			 * ... for the worker: what you should do
			 * ... for the request complete callback: what was done
			 */
			struct {
				bool in;
				bool status;
			} state;
			struct work_struct work;
		} ctrl;
	} g;
	struct {
		struct usb_device *udev;
	} h;
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
	int err;
	struct usbtunnel *ut;

	dev_dbg(&udev->dev, "<%s>\n", __func__);
	ut = usbtunnel_lookup(dev_name(&udev->dev));
	if (!ut)
		return -ENODEV;
	ut->h.udev = udev;
	dev_set_drvdata(&udev->dev, ut);
	/* TODO */
	ut->g.driver.max_speed = udev->speed;
	err = usb_gadget_probe_driver(&ut->g.driver);
	if (err)
		return err;
	dev_info(&udev->dev, "tunnel is running on port\n");
	return 0;
}

static void usbtunnel_host_disconnect(struct usb_device *udev)
{
	struct usbtunnel *ut;

	dev_dbg(&udev->dev, "<%s>\n", __func__);
	ut = dev_get_drvdata(&udev->dev);
	usb_gadget_unregister_driver(&ut->g.driver);
	ut->h.udev = NULL;
	dev_info(&udev->dev, "tunnel disconnected from port\n");
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

static void usbtunnel_ep0_req_complete(struct usb_ep *ep,
		struct usb_request *req)
{
	struct usbtunnel *ut = ep->driver_data;
	int err;

	dev_dbg(&ut->g.gadget->dev, "<%s> in=%d status=%d\n", __func__,
			ut->g.ctrl.state.in, ut->g.ctrl.state.status);
	if (ut->g.ctrl.state.status) {
		/* finished the status phase, so we're done */
		return;
	}
	/* from here on we know we're not in status phase */
	if (ut->g.ctrl.state.in) {
		/* we sent data to the host, receive status */
		ut->g.ctrl.state.in = false;
		ut->g.ctrl.state.status = true;
		ut->g.ep0_req->length = 0;
		err = usb_ep_queue(ut->g.gadget->ep0, ut->g.ep0_req,
				GFP_KERNEL);
		if (err) {
			dev_err(&ut->g.gadget->dev,
					"<%s:%d> usb_ep_queue returned %d\n",
					__func__, __LINE__, err);
			return;
		}
	} else {
		/* FIXME this case is broken */
		/* got data from the host, we can perform the transaction */
		// ut->g.ctrl.state.in = false;
		ut->g.ctrl.state.status = true;
		schedule_work(&ut->g.ctrl.work);
	}
}

static int usbtunnel_gadget_bind(struct usb_gadget *gadget,
		struct usb_gadget_driver *gdriver)
{
	struct usbtunnel *ut = container_of(gdriver, struct usbtunnel,
				g.driver);
	int err;

	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	set_gadget_data(gadget, ut);
	ut->g.gadget = gadget;
	ut->g.ep0_req = usb_ep_alloc_request(gadget->ep0, GFP_KERNEL);
	if (!ut->g.ep0_req) {
		err = -ENOMEM;
		goto fail_usb_ep_alloc_request;
	}
	ut->g.ep0_req->buf = kmalloc(USBTUNNEL_EP0_BUFSIZ, GFP_KERNEL);
	if (!ut->g.ep0_req->buf) {
		err = -ENOMEM;
		goto fail_kmalloc_ep0_req_buf;
	}
	ut->g.ep0_req->complete = usbtunnel_ep0_req_complete;
	ut->g.ep0_req->context = ut;
	gadget->ep0->driver_data = ut;
	return 0;

fail_kmalloc_ep0_req_buf:
	usb_ep_free_request(gadget->ep0, ut->g.ep0_req);
fail_usb_ep_alloc_request:
	return err;
}

static void usbtunnel_gadget_unbind(struct usb_gadget *gadget)
{
	struct usbtunnel *ut = get_gadget_data(gadget);

	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	free_ep_req(gadget->ep0, ut->g.ep0_req);
}

static void usbtunnel_ctrl_work(struct work_struct *work)
{
	struct usbtunnel *ut = container_of(work, struct usbtunnel,
			g.ctrl.work);
	unsigned int pipe;
	int err;

	dev_dbg(&ut->g.gadget->dev, "<%s> in=%d status=%d\n", __func__,
			ut->g.ctrl.state.in, ut->g.ctrl.state.status);
	/* we were called because it's time to perform a control transaction */
	pipe = ut->g.ctrl.state.in ? usb_rcvctrlpipe(ut->h.udev, 0) :
			usb_sndctrlpipe(ut->h.udev, 0);
	err = usb_control_msg(ut->h.udev, pipe, ut->g.ctrl.request,
			ut->g.ctrl.requesttype, ut->g.ctrl.value,
			ut->g.ctrl.index, ut->g.ep0_req->buf, ut->g.ctrl.length,
			USBTUNNEL_CONTROL_MSG_TIMEOUT);
	if (err < 0) {
		dev_err(&ut->h.udev->dev, "usb_control_msg returned %d\n", err);
		return;
	}
	/* we made the transaction. now what? */
	if (ut->g.ctrl.state.status) {
		/* send status */
		ut->g.ep0_req->length = 0;
		err = usb_ep_queue(ut->g.gadget->ep0, ut->g.ep0_req,
				GFP_KERNEL);
		if (err)
			dev_err(&ut->g.gadget->dev,
					"<%s:%d> usb_ep_queue returned %d\n",
					__func__, __LINE__, err);
		return;
	}
	/* from here on we know we're not in status phase */
	if (ut->g.ctrl.state.in) {
		/* we got data from the device, send it to the host */
		ut->g.ep0_req->length = ut->g.ctrl.length;
		err = usb_ep_queue(ut->g.gadget->ep0, ut->g.ep0_req,
				GFP_KERNEL);
		if (err) {
			dev_err(&ut->g.gadget->dev,
					"<%s:%d> usb_ep_queue returned %d\n",
					__func__, __LINE__, err);
			return;
		}
	} else {
		dev_err(&ut->g.gadget->dev, "<%s:%d> shouldn't get here\n",
				__func__, __LINE__);
	}
}

static int usbtunnel_gadget_setup(struct usb_gadget *gadget,
		const struct usb_ctrlrequest *ctrl)
{
	struct usbtunnel *ut = get_gadget_data(gadget);
	int err = 0;

	ut->g.ctrl.requesttype = ctrl->bRequestType;
	ut->g.ctrl.request = ctrl->bRequest;
	ut->g.ctrl.value = le16_to_cpu(ctrl->wValue);
	ut->g.ctrl.index = le16_to_cpu(ctrl->wIndex);
	ut->g.ctrl.length = le16_to_cpu(ctrl->wLength);
	dev_dbg(&gadget->dev, "<%s> 0x%02x 0x%02x 0x%04x 0x%04x 0x%04x\n",
			__func__, ut->g.ctrl.requesttype, ut->g.ctrl.request,
			ut->g.ctrl.value, ut->g.ctrl.index, ut->g.ctrl.length);
	if (ut->g.ctrl.request == USB_REQ_SET_ADDRESS) {
		/*
		 * we don't forward this to the device.
		 * pretend we already did by skipping the worker.
		 */
		ut->g.ctrl.state.in = true;
		ut->g.ctrl.state.status = true;
		ut->g.ep0_req->length = 0 /* which is ut->g.ctrl.length */;
		return usb_ep_queue(gadget->ep0, ut->g.ep0_req, GFP_ATOMIC);
	}
	//switch (ut->g.ctrl.request) {
	//case USB_REQ_SET_ADDRESS:
	//	dev_dbg(&gadget->dev, "<%s> received SET_ADDRESS to %d\n",
	//			__func__, ut->g.ctrl.value);
	//
	//	return 0;
	//case USB_REQ_SET_CONFIGURATION:
	//	/* we hook here to enable/disable endpoints */
	//	/* TODO */
	//	break;
	//}
	if (usb_pipein(ut->g.ctrl.requesttype)) {
		/* perform the transaction first, then return data to host */
		ut->g.ctrl.state.in = true;
		ut->g.ctrl.state.status = false;
		schedule_work(&ut->g.ctrl.work);
	} else if (ut->g.ctrl.length) {
		/* requesttype's direction is OUT */
		/* need data from host before we perform the transaction */
		ut->g.ctrl.state.in = false;
		ut->g.ctrl.state.status = false;
		ut->g.ep0_req->length = ut->g.ctrl.length;
		err = usb_ep_queue(gadget->ep0, ut->g.ep0_req, GFP_ATOMIC);
	} else {
		/* requesttype's direction is OUT. length is nonzero */
		/* "simple" request. no data */
		ut->g.ctrl.state.in = false;
		ut->g.ctrl.state.status = true;
		schedule_work(&ut->g.ctrl.work);
	}
	return err;
}

static void usbtunnel_gadget_disconnect(struct usb_gadget *gadget)
{
	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	/* TODO */
}

static void usbtunnel_gadget_suspend(struct usb_gadget *gadget)
{
	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	/* TODO */
}

static void usbtunnel_gadget_resume(struct usb_gadget *gadget)
{
	dev_dbg(&gadget->dev, "<%s>\n", __func__);
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

	/* TODO max_speed? need to match the port's. what if mismatch? */

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
	pr_debug("<%s> %.*s\n", __func__, len, buf);
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
	snprintf(ut->g.function,  sizeof(ut->g.function), "usbtunnel.%s",
			ut->port);
	pr_info("tunneling port %s to udc %s\n", ut->port, ut->udc);
	ut->g.driver = usbtunnel_gadget_driver_template;
	ut->g.driver.function = ut->g.function;
	ut->g.driver.driver.name = ut->g.function;
	ut->g.driver.udc_name = ut->udc;
	INIT_WORK(&ut->g.ctrl.work, usbtunnel_ctrl_work);
	/* TODO more fields. udc? */

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
	pr_debug("<%s> %s\n", __func__, s);
	ut = usbtunnel_lookup(s);
	kfree(s);
	if (!ut)
		return -EINVAL;
	if (ut->h.udev) {
		pr_err("can't remove tunnel on port %s while active!\n",
				ut->port);
		return -EBUSY;
	}
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
