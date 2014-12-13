#define MODULE_NAME "usblb_glue"
#define pr_fmt(fmt) MODULE_NAME " " fmt

#include <linux/kernel.h>

#include "usblb.h"

#define usblb_bus_info(bus, fmt, ...) \
	pr_info("%s%d: " fmt, KBUILD_MODNAME, (bus)->busnum, __VA_ARGS__)

static void usblb_glue_connect(struct usblb_bus *bus)
{
	usblb_bus_info(bus, "<%s>\n", __func__);
	/* TODO */
}

static void usblb_glue_disconnect(struct usblb_bus *bus)
{
	usblb_bus_info(bus, "<%s>\n", __func__);
	/* TODO */
}

#define usblb_bus_connected(bus) ((bus)->connected_ends == 2)

/* context: bus locked */
void __usblb_spawn_event(struct usblb_bus *bus, enum usblb_event event)
{
	switch (event) {
	case USBLB_E_CONN:
		bus->connected_ends++;
		if (usblb_bus_connected(bus))
			usblb_glue_connect(bus);
		break;
	case USBLB_E_DISC:
		if (usblb_bus_connected(bus))
			usblb_glue_disconnect(bus);
		bus->connected_ends--;
		break;
	}
}
