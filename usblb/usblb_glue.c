#define MODULE_NAME "usblb_glue"
#define pr_fmt(fmt) MODULE_NAME " " fmt

#include <linux/kernel.h>

#include "usblb.h"
#include "usblb_gadget.h"
#include "usblb_host.h"

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

void usblb_glue_transfer_timer_func(unsigned long data)
{
	struct usblb_bus *bus = (void *)data;
	unsigned long flags;
	int status = -EPIPE;

	usblb_bus_lock_irqsave(bus, flags);
	atomic_inc(&bus->in_transfer);
	if (!list_empty(&bus->host.urb_queue)) {
		struct usblb_host_urb_node *node = list_first_entry(
			&bus->host.urb_queue,
			struct usblb_host_urb_node,
			link
		);
		struct urb *urb = node->urb;
		u8 epnum = usb_pipeendpoint(urb->pipe);
		struct usblb_gadget_ep *ep = &bus->gadget.ep[epnum];
		int do_transfer = 1;
		if (!epnum) {
			struct usb_ctrlrequest *setup = \
				(void *)urb->setup_packet;
			if (setup->bRequest == USB_REQ_SET_ADDRESS) {
				usblb_bus_info(bus, "<%s> set address %d\n",
						__func__,
						le16_to_cpu(setup->wValue));
				status = 0;
				do_transfer = 0;
			} else {
				usblb_bus_info(bus, "<%s> setup\n", __func__);
				status = bus->gadget.driver->setup(
					&bus->gadget.g,
					setup
				);
				do_transfer = (status >= 0);
			}
		}
		if (do_transfer) {
			struct usblb_gadget_request *req;
			void *hbuf, *gbuf;
			size_t hlen, glen, len;
			u8 to_host = usb_pipein(urb->pipe);
			if (list_empty(&ep->requests)) /* warn? */
				goto out;
			req = list_first_entry(&ep->requests,
					typeof(*req), link);
			hlen = urb->transfer_buffer_length -
					urb->actual_length;
			glen = req->req.length - req->req.actual;
			usblb_bus_info(bus, "<%s> %s transfer on %s. "
					"hlen = %zu, glen = %zu\n",
					__func__, to_host ? "g2h" : "h2g",
					ep->name, hlen, glen);
			len = min(hlen, glen);
			hbuf = urb->transfer_buffer + urb->actual_length;
			gbuf = req->req.buf + req->req.actual;
			if (to_host)
				memcpy(hbuf, gbuf, len);
			else
				memcpy(gbuf, hbuf, len);
			req->req.status = 0;
			status = 0;
			list_del(&req->link);
			usb_gadget_giveback_request(&ep->ep, &req->req);
		}
		list_del(&node->link);
		kfree(node);
		usb_hcd_unlink_urb_from_ep(bus->host.hcd, urb);
		usb_hcd_giveback_urb(bus->host.hcd, urb, status);
	}
out:
	atomic_dec(&bus->in_transfer);
	usblb_bus_unlock_irqrestore(bus, flags);

	if (atomic_read(&bus->transfer_active))
		mod_timer(
			&bus->transfer_timer,
			jiffies + USBLB_TRANSFER_INTERVAL_JIFFIES
		);
}

void usblb_glue_cleanup_queues(struct usblb_bus *bus)
{
	struct usblb_host_urb_node *node, *tmp;
	struct urb *urb;
	unsigned long flags;
	usblb_bus_lock_irqsave(bus, flags);
	list_for_each_entry_safe(node, tmp, &bus->host.urb_queue, link) {
		urb = node->urb;
		usb_hcd_unlink_urb_from_ep(bus->host.hcd, urb);
		usb_hcd_giveback_urb(bus->host.hcd, urb, -EPIPE);
		list_del(&node->link);
		kfree(node);
	}
	usblb_bus_unlock_irqrestore(bus, flags);
}
