#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/usb/composite.h>

#include "u_f.h"

#include "usbticker.h"

#define TICKER_DEFAULT_INTERVAL_MS 1000

struct f_ticker_opts {
	struct usb_function_instance func_inst;
	u32 interval;
};

static inline struct f_ticker_opts *to_f_ticker_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_ticker_opts,
		func_inst.group);
}

struct f_ticker {
	struct usb_function func;
	struct usb_ep *ep;
	struct usb_request *req;
	struct timer_list timer;
	atomic_t active;
	u32 interval;
	u32 count;
	spinlock_t count_lock;
};

static inline struct f_ticker *func_to_ticker(struct usb_function *f)
{
	return container_of(f, struct f_ticker, func);
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor ticker_intf = {
	.bLength =		sizeof ticker_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor ticker_fs_int_desc = {
	.bLength =          USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =  USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes =     USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =   cpu_to_le16(sizeof(__le32)),
	.bInterval =        TICKER_POLL_INTERVAL_MS,
};

static struct usb_descriptor_header *ticker_fs_descs[] = {
	(struct usb_descriptor_header *) &ticker_intf,
	(struct usb_descriptor_header *) &ticker_fs_int_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor ticker_hs_int_desc = {
	.bLength =          USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =  USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes =     USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =   cpu_to_le16(sizeof(__le32)),
	.bInterval =        TICKER_POLL_INTERVAL_MS,
};

static struct usb_descriptor_header *ticker_hs_descs[] = {
	(struct usb_descriptor_header *) &ticker_intf,
	(struct usb_descriptor_header *) &ticker_hs_int_desc,
	NULL,
};

/* super speed support: */

static struct usb_endpoint_descriptor ticker_ss_int_desc = {
	.bLength =          USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =  USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes =     USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =   cpu_to_le16(sizeof(__le32)),
	.bInterval =        TICKER_POLL_INTERVAL_MS,
};

static struct usb_descriptor_header *ticker_ss_descs[] = {
	(struct usb_descriptor_header *) &ticker_intf,
	(struct usb_descriptor_header *) &ticker_ss_int_desc,
	NULL,
};

/* function-specific strings: */

static struct usb_string strings_ticker[] = {
	[0].s = "tick every $interval",
	{ } /* end of list */
};

static struct usb_gadget_strings stringtab_ticker = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_ticker,
};

static struct usb_gadget_strings *ticker_strings[] = {
	&stringtab_ticker,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int ticker_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_ticker *ticker = func_to_ticker(f);
	int status;

	pr_debug("<%s>\n", __func__);

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	ticker_intf.bInterfaceNumber = status;

	status = usb_string_id(cdev);
	if (status < 0)
		return status;
	strings_ticker[0].id = status;
	ticker_intf.iInterface = status;

	ticker->ep = usb_ep_autoconfig(cdev->gadget, &ticker_fs_int_desc);
	if (!ticker->ep) {
		ERROR(cdev, "%s: can't autoconfigure on %s\n",
				f->name, cdev->gadget->name);
		return -ENODEV;
	}
	/* support high speed hardware */
	ticker_hs_int_desc.bEndpointAddress = \
		ticker_fs_int_desc.bEndpointAddress;
	/* support super speed hardware */
	ticker_ss_int_desc.bEndpointAddress = \
		ticker_ss_int_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, ticker_fs_descs, ticker_hs_descs,
			ticker_ss_descs);
	if (status)
		return status;

	dev_info(&cdev->gadget->dev,
			"%s speed INT/%s\n",
			gadget_is_superspeed(c->cdev->gadget) ? "super" :
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			ticker->ep->name);
	return 0;
}

static void ticker_unbind(struct usb_configuration *c, struct usb_function *f)
{
	pr_debug("<%s>\n", __func__);
	usb_free_all_descriptors(f);
}

static void ticker_free_func(struct usb_function *f)
{
	struct f_ticker *ticker = func_to_ticker(f);
	pr_debug("<%s>\n", __func__);
	kfree(ticker);
}

static inline void f_ticker_fire_timer(struct f_ticker *ticker) {
	mod_timer(
		&ticker->timer,
		jiffies +msecs_to_jiffies(ticker->interval)
	);
}

static void ticker_req_complete(struct usb_ep *ep, struct usb_request *req)
{
	pr_debug("<%s>\n", __func__);
}

static void f_ticker_timer_func(unsigned long data)
{
	struct f_ticker *ticker = (void *)data;
	__le32 count;
	unsigned long flags;
	int err;

	pr_debug("<%s>\n", __func__);

	spin_lock_irqsave(&ticker->count_lock, flags);
	count = cpu_to_le32(ticker->count++);
	memcpy(ticker->req->buf, &count, sizeof(count));
	err = usb_ep_queue(ticker->ep, ticker->req, GFP_ATOMIC);
	spin_unlock_irqrestore(&ticker->count_lock, flags);

	if (err)
		pr_err("<%s> usb_ep_queue failed, err = %d\n", __func__, err);

	if (atomic_read(&ticker->active))
		f_ticker_fire_timer(ticker);
}

static int f_ticker_enable(struct f_ticker *ticker,
		struct usb_composite_dev *cdev)
{
	int err;

	pr_debug("<%s>\n", __func__);

	if (!ticker->ep->desc &&
			config_ep_by_speed(
				cdev->gadget,
				&ticker->func,
				ticker->ep
			))
		return -EINVAL;
	usb_ep_enable(ticker->ep);

	ticker->ep->driver_data = ticker;
	atomic_set(&ticker->active, 1);

	ticker->req = usb_ep_alloc_request(ticker->ep, GFP_ATOMIC);
	if (!ticker->req) {
		err = -ENOMEM;
		pr_err("<%s> usb_ep_alloc_request failed\n", __func__);
		goto fail_usb_ep_alloc_request;
	}
	ticker->req->buf = kmalloc(sizeof(ticker->count), GFP_ATOMIC);
	if (!ticker->req->buf) {
		err = -ENOMEM;
		pr_err("<%s> failed to allocate request buffer\n", __func__);
		goto fail_kmalloc_req_buf;
	}
	ticker->req->complete = ticker_req_complete;
	ticker->req->context = ticker;
	ticker->req->length = sizeof(ticker->count);

	f_ticker_fire_timer(ticker);
	return 0;

fail_kmalloc_req_buf:
	usb_ep_free_request(ticker->ep, ticker->req);
fail_usb_ep_alloc_request:
	return err;
}

static void f_ticker_disable(struct f_ticker *ticker)
{
	pr_debug("<%s>\n", __func__);
	atomic_set(&ticker->active, 0);
	del_timer_sync(&ticker->timer);
	usb_ep_disable(ticker->ep);
	ticker->ep->driver_data = NULL;
	usb_ep_dequeue(ticker->ep, ticker->req);
	kfree(ticker->req->buf);
	usb_ep_free_request(ticker->ep, ticker->req);
}

static void ticker_disable(struct usb_function *f)
{
	struct f_ticker *ticker = func_to_ticker(f);
	pr_debug("<%s>\n", __func__);
	f_ticker_disable(ticker);
}

static int ticker_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_ticker *ticker = func_to_ticker(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	pr_debug("<%s>\n", __func__);
	/* we know alt is zero */
	if (ticker->ep->driver_data)
		f_ticker_disable(ticker);
	return f_ticker_enable(ticker, cdev);
}

static struct usb_function *ticker_alloc_func(struct usb_function_instance *fi)
{
	struct f_ticker *ticker;
	struct f_ticker_opts *opts = \
		container_of(fi, typeof(*opts), func_inst);
	struct usb_function *func;
	int err;

	pr_debug("<%s>\n", __func__);

	ticker = kzalloc(sizeof(*ticker), GFP_KERNEL);
	if (!ticker) {
		err = -ENOMEM;
		pr_err("<%s> failed to allocate function\n", __func__);
		goto fail_kzalloc_ticker;
	}
	ticker->interval = opts->interval;
	ticker->count = 0;
	atomic_set(&ticker->active, 0);
	spin_lock_init(&ticker->count_lock);
	setup_timer(&ticker->timer, f_ticker_timer_func, (unsigned long)ticker);

	func = &ticker->func;
	func->name = "ticker";
	func->strings = ticker_strings;
	func->bind = ticker_bind;
	func->unbind = ticker_unbind;
	func->free_func = ticker_free_func;
	func->disable = ticker_disable;
	func->set_alt = ticker_set_alt;

	return func;

fail_kzalloc_ticker:
	return ERR_PTR(err);
}

CONFIGFS_ATTR_STRUCT(f_ticker_opts);
CONFIGFS_ATTR_OPS(f_ticker_opts);

static void ticker_attr_release(struct config_item *item)
{
	struct f_ticker_opts *opts = to_f_ticker_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations ticker_item_ops = {
	.release		= ticker_attr_release,
	.show_attribute		= f_ticker_opts_attr_show,
	.store_attribute	= f_ticker_opts_attr_store,
};

static ssize_t f_ticker_opts_interval_show(struct f_ticker_opts *opts,
		char *page)
{
	return sprintf(page, "%u\n", opts->interval);
}

static ssize_t f_ticker_opts_interval_store(struct f_ticker_opts *opts,
		const char *page, size_t len)
{
	int ret;
	u32 num;
	ret = kstrtou32(page, 0, &num);
	if (ret)
		return ret;
	if (num % TICKER_POLL_INTERVAL_MS) {
		pr_err("f_ticker_opts interval must be a multiple of %d\n",
				TICKER_POLL_INTERVAL_MS);
		return -EINVAL;
	}
	opts->interval = num;
	ret = len;
	return ret;
}

static struct f_ticker_opts_attribute f_ticker_opts_interval =
	__CONFIGFS_ATTR(interval, S_IRUGO | S_IWUSR,
			f_ticker_opts_interval_show,
			f_ticker_opts_interval_store);

static struct configfs_attribute *ticker_attrs[] = {
	&f_ticker_opts_interval.attr,
	NULL,
};

static struct config_item_type ticker_func_type = {
	.ct_item_ops = &ticker_item_ops,
	.ct_attrs = ticker_attrs,
	.ct_owner = THIS_MODULE,
};

static void ticker_free_instance(struct usb_function_instance *fi)
{
	struct f_ticker_opts *opts = \
		container_of(fi, typeof(*opts), func_inst);
	pr_debug("<%s>\n", __func__);
	kfree(opts);
}

static struct usb_function_instance *ticker_alloc_instance(void)
{
	struct f_ticker_opts *opts;
	int err;

	pr_debug("<%s>\n", __func__);

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts) {
		err = -ENOMEM;
		pr_err("<%s> failed to allocate instance\n", __func__);
		goto fail_kzalloc_fti;
	}
	opts->interval = TICKER_DEFAULT_INTERVAL_MS;
	opts->func_inst.free_func_inst = ticker_free_instance;
	config_group_init_type_name(&opts->func_inst.group, "",
			&ticker_func_type);
	return &opts->func_inst;

fail_kzalloc_fti:
	return ERR_PTR(err);
}

DECLARE_USB_FUNCTION_INIT(ticker, ticker_alloc_instance, ticker_alloc_func);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Ticker usb gadget function");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
