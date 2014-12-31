#define MODULE_NAME "usblb_glue"
#define pr_fmt(fmt) MODULE_NAME " " fmt

#include <linux/kernel.h>

#include "usblb.h"
#include "usblb_gadget.h"

#define usblb_bus_info(bus, fmt, ...) \
	pr_info("%s%d: " fmt, KBUILD_MODNAME, (bus)->busnum, __VA_ARGS__)

#define USBLB_GLUE_RESET_TIME_MSEC 100

/* context: bus locked */
static void usblb_glue_connect(struct usblb_bus *bus)
{
	usblb_bus_info(bus, "<%s>\n", __func__);
	bus->host.port1_status.wPortChange |= USB_PORT_STAT_C_CONNECTION;
	bus->host.port1_status.wPortStatus |= USB_PORT_STAT_CONNECTION;
	bus->host.port1_status.wPortStatus |= USB_PORT_STAT_HIGH_SPEED;
	usb_hcd_poll_rh_status(bus->host.hcd);
}

/* context: bus locked */
static void usblb_glue_disconnect(struct usblb_bus *bus)
{
	usblb_bus_info(bus, "<%s>\n", __func__);
	bus->host.port1_status.wPortChange |= USB_PORT_STAT_C_CONNECTION;
	bus->host.port1_status.wPortStatus &= ~USB_PORT_STAT_ENABLE;
	bus->host.port1_status.wPortStatus &= ~USB_PORT_STAT_HIGH_SPEED;
	bus->host.port1_status.wPortStatus &= ~USB_PORT_STAT_CONNECTION;
	usb_hcd_poll_rh_status(bus->host.hcd);
}

static void usblb_glue_finish_reset(unsigned long data)
{
	struct usblb_bus *bus = (void *)data;
	unsigned long flags;

	usblb_bus_info(bus, "<%s>\n", __func__);
	usblb_bus_lock_irqsave(bus, flags);
	atomic_inc(&bus->event);
	bus->host.port1_status.wPortChange |= USB_PORT_STAT_C_ENABLE;
	bus->host.port1_status.wPortStatus |= USB_PORT_STAT_ENABLE;
	bus->host.port1_status.wPortChange |= USB_PORT_STAT_C_RESET;
	bus->host.port1_status.wPortStatus &= ~USB_PORT_STAT_RESET;
	usb_hcd_poll_rh_status(bus->host.hcd);
	/* TODO restart gadget */
	atomic_dec(&bus->event);
	usblb_bus_unlock_irqrestore(bus, flags);
}

static void usblb_glue_reset(struct usblb_bus *bus)
{
	bus->host.port1_status.wPortStatus &= ~USB_PORT_STAT_ENABLE;
	setup_timer(&bus->host.reset_timer, usblb_glue_finish_reset,
			(unsigned long)bus);
	mod_timer(
		&bus->host.reset_timer,
		jiffies + msecs_to_jiffies(USBLB_GLUE_RESET_TIME_MSEC)
	);
}

#define usblb_bus_connected(bus) ((bus)->connected_ends == 2)

/* context: bus locked */
void __usblb_spawn_event(struct usblb_bus *bus, enum usblb_event event)
{
	atomic_inc(&bus->event);
	switch (event) {
	case USBLB_E_CONNECT:
		bus->connected_ends++;
		if (usblb_bus_connected(bus))
			usblb_glue_connect(bus);
		break;
	case USBLB_E_DISCONNECT:
		if (usblb_bus_connected(bus))
			usblb_glue_disconnect(bus);
		bus->connected_ends--;
		break;
	case USBLB_E_RESET:
		usblb_glue_reset(bus);
		break;
	}
	atomic_dec(&bus->event);
}

struct usblb_glue_transfer {
	struct list_head link;
	void *data;
};
#define usblb_glue_transfer_list_first_entry(list) \
	list_first_entry(list, struct usblb_glue_transfer, link)

static int usblb_glue_transfer_queue(struct list_head *list, void *data)
{
	struct usblb_glue_transfer *t;
	t = kzalloc(sizeof(*t), GFP_ATOMIC);
	if (!t)
		return -ENOMEM;
	t->data = data;
	list_add_tail(&t->link, list);
	return 0;
}

/* context: bus locked */
int usblb_glue_transfer_g2h(struct usblb_gadget *gadget,
		struct usb_request *req)
{
	return usblb_glue_transfer_queue(&usblb_gadget_to_bus(gadget)->g2h,
			to_usblb_gadget_request(req));
}

/* context: bus locked */
int usblb_glue_transfer_h2g(struct usblb_host *host, struct urb *urb)
{
	return usblb_glue_transfer_queue(&usblb_host_to_bus(host)->h2g, urb);
}


void usblb_glue_transfer_timer_func(unsigned long data)
{
	struct usblb_bus *bus = (void *)data;
	unsigned long flags;

	usblb_bus_lock_irqsave(bus, flags);
	if (!list_empty(&bus->h2g)) {
		struct usblb_glue_transfer *t = \
				usblb_glue_transfer_list_first_entry(
					&bus->h2g
				);
		struct urb *urb = t->data;
		u8 epnum = usb_pipeendpoint(urb->pipe);
		struct usblb_gadget_ep *ep = &bus->gadget.ep[epnum];
		usblb_bus_info(bus, "<%s> h2g transfer on %s\n",
				__func__, ep->name);
		/* fail the transfer */
		(void)ep; /* suppress warning */
		usb_hcd_unlink_urb_from_ep(bus->host.hcd, urb);
		usb_hcd_giveback_urb(bus->host.hcd, urb, -EPIPE);
		/* TODO actually transfer... */
		list_del(&t->link);
		kfree(t);
	}
	usblb_bus_unlock_irqrestore(bus, flags);

	if (atomic_read(&bus->transfer_timer_active))
		mod_timer(
			&bus->transfer_timer,
			jiffies + USBLB_TRANSFER_INTERVAL_JIFFIES
		);
}
