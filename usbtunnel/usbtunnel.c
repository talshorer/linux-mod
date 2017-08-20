#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>

#include "u_f.h"

#define MAX_DEVNAME_LEN 32
#define USBTUNNEL_EP0_BUFSIZ 1024
/* seems reasonable. we can't allow 5000 because then our host will timeout */
#define USBTUNNEL_CONTROL_MSG_TIMEOUT 2000

struct usbtunnel_pending_ctrl {
	struct usb_ctrlrequest ctrl;
	struct list_head link;
};

struct usbtunnel_ep_map {
	struct {
		unsigned int hep;
		struct usb_ep *gep;
	} map[16];
	unsigned int config;
	struct list_head link;
};

struct usbtunnel {
	char udc[MAX_DEVNAME_LEN + 1];
	char port[MAX_DEVNAME_LEN + 1];
	struct list_head list;
	char function[10 + MAX_DEVNAME_LEN + 1]; /* usbtunnel.$PORT\0 */
	struct usb_gadget_driver gadget_driver;
	struct usb_gadget *gadget;
	struct {
		struct {
			__u8 requesttype;
			__u8 request;
			__u16 value;
			__u16 index;
			__u16 length;
		} setup;
		/*
		 * state:
		 * ... for the worker: what you should do
		 * ... for the request complete callback: what was done
		 */
		struct {
			bool in;
			bool status;
		} state;
		struct {
			spinlock_t lock;
			struct list_head list;
			bool in_progress;
		} pending;
		struct work_struct work;
		struct usb_request *req;
	} ctrl;
	struct usb_device *udev;
	struct list_head maps_list;
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
	ut->udev = udev;
	dev_set_drvdata(&udev->dev, ut);
	/* TODO */
	ut->gadget_driver.max_speed = udev->speed;
	err = usb_gadget_probe_driver(&ut->gadget_driver);
	if (err)
		return err;
	dev_info(&udev->dev, "tunnel is running on port\n");
	return 0;
}

static void usbtunnel_host_disconnect(struct usb_device *udev)
{
	struct usbtunnel *ut;
	struct usbtunnel_ep_map *map, *tmp;

	dev_dbg(&udev->dev, "<%s>\n", __func__);
	ut = dev_get_drvdata(&udev->dev);
	usb_gadget_unregister_driver(&ut->gadget_driver);
	ut->udev = NULL;
	dev_info(&udev->dev, "tunnel disconnected from port\n");
	list_for_each_entry_safe(map, tmp, &ut->maps_list, link)
		kfree(map);
	INIT_LIST_HEAD(&ut->maps_list);
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

/* called with &ut->ctrl.pending.lock locked */
static int usbtunnel_gadget_do_setup(struct usbtunnel *ut,
		const struct usb_ctrlrequest *ctrl)
{
	int err = 0;

	ut->ctrl.pending.in_progress = true;
	ut->ctrl.setup.requesttype = ctrl->bRequestType;
	ut->ctrl.setup.request = ctrl->bRequest;
	ut->ctrl.setup.value = le16_to_cpu(ctrl->wValue);
	ut->ctrl.setup.index = le16_to_cpu(ctrl->wIndex);
	ut->ctrl.setup.length = le16_to_cpu(ctrl->wLength);
	dev_dbg(&ut->gadget->dev, "<%s> 0x%02x 0x%02x 0x%04x 0x%04x 0x%04x\n",
		__func__, ut->ctrl.setup.requesttype,
		ut->ctrl.setup.request, ut->ctrl.setup.value,
		ut->ctrl.setup.index, ut->ctrl.setup.length);
	if (ut->ctrl.setup.request == USB_REQ_SET_ADDRESS) {
		/*
		 * we don't forward this to the device.
		 * pretend we already did by skipping the worker.
		 */
		ut->ctrl.state.in = true;
		ut->ctrl.state.status = true;
		ut->ctrl.req->length = 0 /* which is ut->ctrl.length */;
		return usb_ep_queue(ut->gadget->ep0, ut->ctrl.req,
			GFP_ATOMIC);
	}
	if (usb_pipein(ut->ctrl.setup.requesttype)) {
		/* perform the transaction first, then return data to host */
		ut->ctrl.state.in = true;
		ut->ctrl.state.status = false;
		schedule_work(&ut->ctrl.work);
	} else if (ut->ctrl.setup.length) {
		/* requesttype's direction is OUT */
		/* need data from host before we perform the transaction */
		ut->ctrl.state.in = false;
		ut->ctrl.state.status = false;
		ut->ctrl.req->length = ut->ctrl.setup.length;
		err = usb_ep_queue(ut->gadget->ep0, ut->ctrl.req,
			GFP_ATOMIC);
	} else {
		/* requesttype's direction is OUT. length is zero */
		/* "simple" request. no data */
		ut->ctrl.state.in = false;
		ut->ctrl.state.status = true;
		schedule_work(&ut->ctrl.work);
	}
	return err;
}

static void usbtunnel_gadget_process_pending_ctrl(struct usbtunnel *ut)
{
	unsigned long flags;
	struct usbtunnel_pending_ctrl *pending;

	spin_lock_irqsave(&ut->ctrl.pending.lock, flags);
	if (list_empty(&ut->ctrl.pending.list)) {
		ut->ctrl.pending.in_progress = false;
	} else {
		pending = list_first_entry(&ut->ctrl.pending.list,
			typeof(*pending), link);
		usbtunnel_gadget_do_setup(ut, &pending->ctrl);
		list_del(&pending->link);
		kfree(pending);
	}
	spin_unlock_irqrestore(&ut->ctrl.pending.lock, flags);
}

static void usbtunnel_ep0_req_complete(struct usb_ep *ep,
		struct usb_request *req)
{
	struct usbtunnel *ut = ep->driver_data;
	int err;

	dev_dbg(&ut->gadget->dev, "<%s> in=%d status=%d\n", __func__,
		ut->ctrl.state.in, ut->ctrl.state.status);
	if (ut->ctrl.state.status) {
		/* finished the status phase, so we're done */
		usbtunnel_gadget_process_pending_ctrl(ut);
		return;
	}
	/* from here on we know we're not in status phase */
	if (ut->ctrl.state.in) {
		/* we sent data to the host, receive status */
		ut->ctrl.state.in = false;
		ut->ctrl.state.status = true;
		ut->ctrl.req->length = 0;
		err = usb_ep_queue(ut->gadget->ep0, ut->ctrl.req,
				GFP_ATOMIC);
		if (err) {
			dev_err(&ut->gadget->dev,
				"<%s:%d> usb_ep_queue returned %d\n",
				__func__, __LINE__, err);
			return;
		}
	} else {
		/* got data from the host, we can perform the transaction */
		// ut->ctrl.state.in = false;
		ut->ctrl.state.status = true;
		schedule_work(&ut->ctrl.work);
	}
}

static int usbtunnel_gadget_bind(struct usb_gadget *gadget,
		struct usb_gadget_driver *gdriver)
{
	struct usbtunnel *ut = container_of(gdriver, struct usbtunnel,
		gadget_driver);
	int err;

	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	set_gadget_data(gadget, ut);
	ut->gadget = gadget;
	ut->ctrl.req = usb_ep_alloc_request(gadget->ep0, GFP_KERNEL);
	if (!ut->ctrl.req) {
		err = -ENOMEM;
		goto fail_usb_ep_alloc_request;
	}
	ut->ctrl.req->buf = kmalloc(USBTUNNEL_EP0_BUFSIZ, GFP_KERNEL);
	if (!ut->ctrl.req->buf) {
		err = -ENOMEM;
		goto fail_kmalloc_ctrl_req_buf;
	}
	ut->ctrl.req->complete = usbtunnel_ep0_req_complete;
	ut->ctrl.req->context = ut;
	gadget->ep0->driver_data = ut;
	return 0;

fail_kmalloc_ctrl_req_buf:
	usb_ep_free_request(gadget->ep0, ut->ctrl.req);
fail_usb_ep_alloc_request:
	return err;
}

static void usbtunnel_gadget_unbind(struct usb_gadget *gadget)
{
	struct usbtunnel *ut = get_gadget_data(gadget);

	dev_dbg(&gadget->dev, "<%s>\n", __func__);
	free_ep_req(gadget->ep0, ut->ctrl.req);
}

static void usbtunnel_set_configuration_hook(struct usbtunnel *ut)
{
	struct usb_host_config *config;
	struct usb_interface *interface;
	struct usb_host_endpoint *ep;
	unsigned int i, j;

	for (i = 0; i < ut->udev->descriptor.bNumConfigurations; i++)
		if (ut->udev->config[i].desc.bConfigurationValue ==
				ut->ctrl.setup.value) {
			config = &ut->udev->config[i];
			break;
		}
	if (i == ut->udev->descriptor.bNumConfigurations) {
		dev_warn(&ut->udev->dev, "configuration %d not found\n",
			ut->ctrl.setup.value);
		/* TODO do more? nothing will work if we just roll with this. */
		return;
	}
	dev_info(&ut->gadget->dev, "USB_REQ_SET_CONFIGURATION\n");
	dev_info(&ut->udev->dev, "bNumConfigurations = %d\n",
		ut->udev->descriptor.bNumConfigurations);
	dev_info(&ut->udev->dev, "bNumInterfaces = %d\n",
		config->desc.bNumInterfaces);
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		interface = config->interface[i];
		dev_info(&ut->udev->dev, "interface@%d: bNumEndpoints = %d\n",
			i, interface->cur_altsetting->desc.bNumEndpoints);
		for (j = 0; j < interface->cur_altsetting->desc.bNumEndpoints;
				j++) {
			ep = &interface->cur_altsetting->endpoint[j];
			dev_info(&ut->udev->dev,
				"ep@%d.%d: bEndpointAddress 0x%02x\n",
				i, j, ep->desc.bEndpointAddress);
		}
	}
}

static int usbtunnel_translate_configuration(struct usbtunnel *ut)
{
	void *buf = ut->ctrl.req->buf;
	struct usb_config_descriptor *cdesc = buf;
	unsigned int i, j, x;
	struct usb_interface_descriptor *idesc;
	struct usb_endpoint_descriptor *edesc;
	__le16 maxpacket; /* usb_ep_autoconfig might mess wMaxPacketSize */
	struct usbtunnel_ep_map *map;
	bool populate_map = true;
	int err;

	if (le16_to_cpu(cdesc->wTotalLength) > ut->ctrl.setup.length)
		return 0;
	list_for_each_entry(map, &ut->maps_list, link)
		if (map->config == cdesc->bConfigurationValue) {
			populate_map = false;
			break;
		}
	if (populate_map) {
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map)
			return -ENOMEM;
		map->config = cdesc->bConfigurationValue;
		dev_info(&ut->udev->dev,
			"mapping endpoints for configuration %d\n",
			map->config);
	}
	x = 0;
	buf += cdesc->bLength;
	for (i = 0; i < cdesc->bNumInterfaces; i++) {
		do {
			idesc = buf;
			buf += idesc->bLength;
		} while (idesc->bDescriptorType != USB_DT_INTERFACE);
		for (j = 0; j < idesc->bNumEndpoints; j++) {
			do {
				edesc = buf;
				buf += edesc->bLength;
			} while (edesc->bDescriptorType != USB_DT_ENDPOINT);
			if (populate_map) {
				maxpacket =  edesc->wMaxPacketSize;
				map->map[x].hep = edesc->bEndpointAddress;
				map->map[x].gep = usb_ep_autoconfig(
					ut->gadget, edesc);
				if (!map->map[x].gep) {
					err = -EBUSY;
					goto out;
				}
				edesc->wMaxPacketSize = maxpacket;
				dev_info(&ut->udev->dev,
					"host ep 0x%02x -> gadget ep 0x%02x\n",
					map->map[x].hep,
					edesc->bEndpointAddress);
			} else {
				edesc->bEndpointAddress =
					map->map[x].gep->address;
			}
			x++;
		}
	}
	err = 0;
out:
	if (populate_map) {
		usb_ep_autoconfig_reset(ut->gadget);
		if (err)
			kfree(map);
		else
			list_add(&map->link, &ut->maps_list);
	}
	return err;
}

static void usbtunnel_ctrl_work(struct work_struct *work)
{
	struct usbtunnel *ut = container_of(work, struct usbtunnel,
		ctrl.work);
	unsigned int pipe;
	int err;

	dev_dbg(&ut->gadget->dev, "<%s> in=%d status=%d\n", __func__,
		ut->ctrl.state.in, ut->ctrl.state.status);
	/* we were called because it's time to perform a control transaction */
	pipe = ut->ctrl.state.in ? usb_rcvctrlpipe(ut->udev, 0) :
		usb_sndctrlpipe(ut->udev, 0);
	if (ut->ctrl.setup.request == USB_REQ_SET_CONFIGURATION) {
		err = usb_set_configuration(ut->udev, ut->ctrl.setup.value);
		if (!err)
			usbtunnel_set_configuration_hook(ut);
	} else {
		err = usb_control_msg(ut->udev, pipe,
			ut->ctrl.setup.request, ut->ctrl.setup.requesttype,
			ut->ctrl.setup.value, ut->ctrl.setup.index,
			ut->ctrl.req->buf, ut->ctrl.setup.length,
			USBTUNNEL_CONTROL_MSG_TIMEOUT);
		if (err >= 0 && (ut->ctrl.setup.value >> 8) ==
				USB_DT_CONFIG && ut->ctrl.setup.request ==
					USB_REQ_GET_DESCRIPTOR)
			err = usbtunnel_translate_configuration(ut);
	}
	if (err < 0) {
		dev_err(&ut->udev->dev, "usb_control_msg returned %d\n", err);
		return;
	}
	/* we made the transaction. now what? */
	if (ut->ctrl.state.status) {
		if (ut->ctrl.state.in || !ut->ctrl.setup.length) {
			/* send status */
			ut->ctrl.req->length = 0;
			err = usb_ep_queue(ut->gadget->ep0, ut->ctrl.req,
				GFP_KERNEL);
			if (err)
				dev_err(&ut->gadget->dev,
					"<%s:%d> usb_ep_queue returned %d\n",
					__func__, __LINE__, err);
		}
		usbtunnel_gadget_process_pending_ctrl(ut);
		return;
	}
	/* from here on we know we're not in status phase */
	if (ut->ctrl.state.in) {
		/* we got data from the device, send it to the host */
		ut->ctrl.req->length = ut->ctrl.setup.length;
		err = usb_ep_queue(ut->gadget->ep0, ut->ctrl.req,
			GFP_KERNEL);
		if (err) {
			dev_err(&ut->gadget->dev,
				"<%s:%d> usb_ep_queue returned %d\n",
				__func__, __LINE__, err);
			return;
		}
	} else {
		dev_err(&ut->gadget->dev, "<%s:%d> shouldn't get here\n",
			__func__, __LINE__);
	}
}

static int usbtunnel_gadget_setup(struct usb_gadget *gadget,
		const struct usb_ctrlrequest *ctrl)
{
	struct usbtunnel *ut = get_gadget_data(gadget);
	struct usbtunnel_pending_ctrl *pending;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ut->ctrl.pending.lock, flags);
	if (!ut->ctrl.pending.in_progress) {
		ret = usbtunnel_gadget_do_setup(ut, ctrl);
	} else {
		pending = kmalloc(sizeof(*pending), GFP_ATOMIC);
		if (!pending) {
			ret = -ENOMEM;
		} else {
			memcpy(&pending->ctrl, ctrl, sizeof(*ctrl));
			list_add_tail(&pending->link, &ut->ctrl.pending.list);
			ret = 0;
		}
	}
	spin_unlock_irqrestore(&ut->ctrl.pending.lock, flags);
	return ret;
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
	snprintf(ut->function,  sizeof(ut->function), "usbtunnel.%s",
		ut->port);
	pr_info("tunneling port %s to udc %s\n", ut->port, ut->udc);
	ut->gadget_driver = usbtunnel_gadget_driver_template;
	ut->gadget_driver.function = ut->function;
	ut->gadget_driver.driver.name = ut->function;
	ut->gadget_driver.udc_name = ut->udc;
	INIT_WORK(&ut->ctrl.work, usbtunnel_ctrl_work);
	spin_lock_init(&ut->ctrl.pending.lock);
	INIT_LIST_HEAD(&ut->ctrl.pending.list);
	ut->ctrl.pending.in_progress = false;
	INIT_LIST_HEAD(&ut->maps_list);

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
	if (ut->udev) {
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
MODULE_VERSION("0.0.2");
