#define MODULE_NAME "usblb_glue"
#define pr_fmt(fmt) MODULE_NAME " " fmt

#include <linux/kernel.h>

#include "usblb.h"

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
