#define pr_fmt(fmt) KBUILD_BASENAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "usblb_gadget.h"

#define USBLB_GADGET_EP_NUM 16
#define USBLB_GADGET_MAXPACKET 1024

static struct class *usblb_gadget_class;

int usblb_gadget_init(void)
{
	usblb_gadget_class = class_create(THIS_MODULE, KBUILD_BASENAME);
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

static int usblb_gadget_enable(struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc)
{
	dev_info(to_usblb_gadget_ep(ep)->g->dev,
			"<%s> on %s\n", __func__, ep->name);
	return 0;
}

static int usblb_gadget_disable(struct usb_ep *ep)
{
	dev_info(to_usblb_gadget_ep(ep)->g->dev,
			"<%s> on %s\n", __func__, ep->name);
	return 0;
}

static int __usblb_gadget_queue(struct usblb_gadget_ep *ep,
		struct usblb_gadget_request *req)
{
	struct usblb_gadget *gadget = ep->g;
	int ret = 0;

	if (!atomic_read(&usblb_gadget_to_bus(gadget)->transfer_active))
		ret = -EPIPE;
	else
		list_add_tail(&req->link, &ep->requests);

	return ret;
}

static int usblb_gadget_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t gfp_flags)
{
	struct usblb_gadget_request *req = to_usblb_gadget_request(_req);
	struct usblb_gadget_ep *ep = to_usblb_gadget_ep(_ep);
	struct usblb_gadget *gadget = ep->g;
	unsigned long flags;
	int ret;

	dev_info(gadget->dev, "<%s> on %s\n", __func__, ep->name);

	_req->actual = 0;

	if (!atomic_read(&usblb_gadget_to_bus(gadget)->in_transfer)) {
		usblb_gadget_lock_irqsave(gadget, flags);
		ret = __usblb_gadget_queue(ep, req);
		usblb_gadget_unlock_irqrestore(gadget, flags);
	} else {
		ret = __usblb_gadget_queue(ep, req);
	}

	return ret;
}

static int usblb_gadget_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct usblb_gadget_request *req = to_usblb_gadget_request(_req);
	struct usblb_gadget_ep *ep = to_usblb_gadget_ep(_ep);
	struct usblb_gadget *gadget = ep->g;
	int ret;
	unsigned long flags;

	dev_info(gadget->dev, "<%s> on %s\n", __func__, ep->name);

	usblb_gadget_lock_irqsave(gadget, flags);
	if (req->link.next == LIST_POISON1)
		ret = -EINVAL;
	else {
		ret = 0;
		list_del(&req->link);
	}
	usblb_gadget_unlock_irqrestore(gadget, flags);

	return 0;
}

static struct usb_request *usblb_gadget_alloc_request(struct usb_ep *ep,
		gfp_t gfp_flags)
{
	struct usblb_gadget_request *req;

	dev_info(to_usblb_gadget_ep(ep)->g->dev,
			"<%s> on %s\n", __func__, ep->name);
	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;
	return &req->req;
}

static void usblb_gadget_free_request(struct usb_ep *ep,
		struct usb_request *_req)
{
	struct usblb_gadget_request *req;

	dev_info(to_usblb_gadget_ep(ep)->g->dev,
			"<%s> on %s\n", __func__, ep->name);
	req = to_usblb_gadget_request(_req);
	kfree(req);
}

static const struct usb_ep_ops usblb_gadget_ep_ops = {
	.enable         = usblb_gadget_enable,
	.disable        = usblb_gadget_disable,
	.queue          = usblb_gadget_queue,
	.dequeue        = usblb_gadget_dequeue,
#if 0 /* TODO */
	.set_halt       = usblb_gadget_set_halt,
	.set_wedge      = usblb_gadget_set_wedge,
	.fifo_status    = usblb_gadget_fifo_status,
	.fifo_flush     = usblb_gadget_fifo_flush
#endif /* 0 */
	.alloc_request  = usblb_gadget_alloc_request,
	.free_request   = usblb_gadget_free_request,
};

static void usblb_gadget_ep_init(struct usblb_gadget_ep *ep, int epnum)
{
	sprintf(ep->name, "ep%d", epnum);
	ep->ep.name = ep->name;
	INIT_LIST_HEAD(&ep->ep.ep_list);
	INIT_LIST_HEAD(&ep->requests);
	ep->ep.ops = &usblb_gadget_ep_ops;
	if (epnum) {
		usb_ep_set_maxpacket_limit(&ep->ep, USBLB_GADGET_MAXPACKET);
		list_add_tail(&ep->ep.ep_list, &ep->g->g.ep_list);
	} else {
		usb_ep_set_maxpacket_limit(&ep->ep, 64);
		ep->g->g.ep0 = &ep->ep;
	}

}

static int usblb_gadget_start(struct usb_gadget *g,
		struct usb_gadget_driver *drv)
{
	struct usblb_gadget *gadget = to_usblb_gadget(g);

	dev_info(gadget->dev, "<%s> with driver \"%s\"\n",
			__func__, drv->function);
	gadget->driver = drv;
	return 0;
}

static int usblb_gadget_stop(struct usb_gadget *g)
{
	dev_info(to_usblb_gadget(g)->dev, "<%s>\n", __func__);
	return 0;
}

static int usblb_gadget_pullup(struct usb_gadget *g, int is_on)
{
	struct usblb_gadget *gadget = to_usblb_gadget(g);
	unsigned long flags;
	enum usblb_event event;

	dev_info(gadget->dev, "<%s> is_on=%d\n", __func__, is_on);

	event = is_on ? USBLB_E_CONNECT : USBLB_E_DISCONNECT;
	usblb_gadget_lock_irqsave(gadget, flags);
	usblb_gadget_spawn_event(gadget, event);
	usblb_gadget_unlock_irqrestore(gadget, flags);
	return 0;
}

static const struct usb_gadget_ops usblb_gadget_operations = {
#if 0 /* TODO */
	.get_frame              = usblb_gadget_get_frame,
	.wakeup                 = usblb_gadget_wakeup,
	.set_selfpowered        = usblb_gadget_set_self_powered,
	.vbus_draw              = usblb_gadget_vbus_draw,
#endif /* 0 */
	.udc_start              = usblb_gadget_start,
	.udc_stop               = usblb_gadget_stop,
	.pullup                 = usblb_gadget_pullup,
};

int usblb_gadget_device_setup(struct usblb_gadget *gadget, int i)
{
	int err;
	int j;

	gadget->dev = device_create(usblb_gadget_class, NULL, MKDEV(0, i),
			gadget, "%s%d", usblb_gadget_class->name, i);
	if (IS_ERR(gadget->dev)) {
		err = PTR_ERR(gadget->dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	gadget->ep = kzalloc(sizeof(gadget->ep[0]) * USBLB_GADGET_EP_NUM,
			GFP_KERNEL);
	if (!gadget->ep) {
		err = -ENOMEM;
		pr_err("failed to allocate endpoints for %s\n",
				dev_name(gadget->dev));
		goto fail_kzalloc_ep;
	}

	gadget->g.ops = &usblb_gadget_operations;
	gadget->g.max_speed = USB_SPEED_HIGH;
	gadget->g.speed = gadget->g.max_speed;
	gadget->g.name = dev_name(gadget->dev);
	INIT_LIST_HEAD(&gadget->g.ep_list);
	for (j = 0; j < USBLB_GADGET_EP_NUM; j++) {
		struct usblb_gadget_ep *ep = &gadget->ep[j];

		ep->g = gadget;
		ep->epnum = j;
		usblb_gadget_ep_init(ep, j);
	}

	err = usb_add_gadget_udc(gadget->dev, &gadget->g);
	if (err) {
		pr_err("usb_add_gadget_udc failed for %s. err = %d\n",
				dev_name(gadget->dev), err);
		goto fail_usb_add_gadget_udc;
	}

	pr_info("created %s successfully\n", dev_name(gadget->dev));
	return 0;

fail_usb_add_gadget_udc:
	kfree(gadget->ep);
fail_kzalloc_ep:
	device_destroy(usblb_gadget_class, gadget->dev->devt);
fail_device_create:
	return err;
}

void usblb_gadget_device_cleanup(struct usblb_gadget *gadget)
{
	pr_info("destroying %s\n", dev_name(gadget->dev));
	usb_del_gadget_udc(&gadget->g);
	kfree(gadget->ep);
	device_destroy(usblb_gadget_class, gadget->dev->devt);
}

int usblb_gadget_set_host(struct usblb_gadget *g, struct usblb_host *h)
{
	int err;

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
