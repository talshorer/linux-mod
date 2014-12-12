#ifndef _USBLB_H
#define _USBLB_H

#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/spinlock.h>

struct usblb_gadget;
struct usblb_host;

/************************************************/
/**************** usblb_gadget.c ****************/
/************************************************/

struct usblb_gadget_io_ep;

struct usblb_gadget {
	struct usblb_host *host;
	struct device *dev;
	struct usb_gadget g;
	struct usblb_gadget_io_ep *ep;
};

extern int usblb_gadget_init(void);
extern void usblb_gadget_exit(void);

extern int usblb_gadget_device_setup(struct usblb_gadget *, int);
extern void usblb_gadget_device_cleanup(struct usblb_gadget *);

extern int usblb_gadget_set_host(struct usblb_gadget *, struct usblb_host *);

enum usblb_gadget_event {
	USBLB_GE_PWRON,
	USBLB_GE_PWROF,
};

extern void __usblb_spawn_gadget_event(struct usblb_gadget *,
		enum usblb_gadget_event);
#define usblb_spawn_gadget_event(host, event) \
	__usblb_spawn_gadget_event(&usblb_host_to_bus(host)->gadget, event)

/************************************************/
/***************** usblb_host.c *****************/
/************************************************/

struct usblb_host {
	struct usblb_gadget *gadget;
	struct device *dev;
	struct usb_hcd *hcd;
	struct usb_port_status port1_status;
};

extern int usblb_host_init(void);
extern void usblb_host_exit(void);

extern int usblb_host_device_setup(struct usblb_host *, int);
extern void usblb_host_device_cleanup(struct usblb_host *);

extern int usblb_host_set_gadget(struct usblb_host *, struct usblb_gadget *);

enum usblb_host_event {
	USBLB_HE_GCONN,
	USBLB_HE_GDISC,
};

extern void __usblb_spawn_host_event(struct usblb_host *,
		enum usblb_host_event);
#define usblb_spawn_host_event(gadget, event) \
	__usblb_spawn_host_event(&usblb_gadget_to_bus(gadget)->host, event)

/************************************************/
/***************** usblb_main.c *****************/
/************************************************/

struct usblb_bus {
	struct usblb_gadget gadget;
	struct usblb_host host;
	spinlock_t lock;
};

#define usblb_gadget_to_bus(_host) \
	container_of(_host, struct usblb_bus, gadget)
#define usblb_host_to_bus(_host) \
	container_of(_host, struct usblb_bus, host)

#define usblb_bus_lock_irqsave(bus, flags) \
	spin_lock_irqsave(&(bus)->lock, flags)
#define usblb_bus_unlock_irqrestore(bus, flags) \
	spin_unlock_irqrestore(&(bus)->lock, flags)

#define usblb_host_lock_irqsave(host, flags) \
	usblb_bus_lock_irqsave(usblb_host_to_bus(host), flags)
#define usblb_host_unlock_irqrestore(host, flags) \
	usblb_bus_unlock_irqrestore(usblb_host_to_bus(host), flags)

#define usblb_gadget_lock_irqsave(gadget, flags) \
	usblb_bus_lock_irqsave(usblb_gadget_to_bus(gadget), flags)
#define usblb_gadget_unlock_irqrestore(gadget, flags) \
	usblb_bus_unlock_irqrestore(usblb_host_to_bus(gadget), flags)

#endif /* _USBLB_H */
