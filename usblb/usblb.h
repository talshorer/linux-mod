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

struct usblb_gadget_ep;

struct usblb_gadget {
	struct device *dev;
	struct usb_gadget g;
	struct usblb_gadget_ep *ep;
	struct usb_gadget_driver *driver;
};

extern int usblb_gadget_init(void);
extern void usblb_gadget_exit(void);

extern int usblb_gadget_device_setup(struct usblb_gadget *, int);
extern void usblb_gadget_device_cleanup(struct usblb_gadget *);

extern int usblb_gadget_set_host(struct usblb_gadget *, struct usblb_host *);

/************************************************/
/***************** usblb_host.c *****************/
/************************************************/

struct usblb_host {
	struct platform_device *pdev;
	struct device *dev;
	struct usb_hcd *hcd;
	struct usb_port_status port1_status;
	struct timer_list reset_timer;
	struct list_head urb_queue;
};

extern int usblb_host_init(void);
extern void usblb_host_exit(void);

extern int usblb_host_device_setup(struct usblb_host *, int);
extern void usblb_host_device_cleanup(struct usblb_host *);

extern int usblb_host_set_gadget(struct usblb_host *, struct usblb_gadget *);

/************************************************/
/***************** usblb_main.c *****************/
/************************************************/

struct usblb_bus {
	struct usblb_gadget gadget;
	struct usblb_host host;
	int busnum;
	spinlock_t lock;
	atomic_t event; /* requires lock to write. readable anytime */
	u8 connected_ends;
	struct timer_list transfer_timer;
	atomic_t transfer_active;
	atomic_t in_transfer; /* requires lock to write. readable anytime */
};

#define usblb_gadget_to_bus(_host) \
	container_of(_host, struct usblb_bus, gadget)
#define usblb_host_to_bus(_host) \
	container_of(_host, struct usblb_bus, host)

#define usblb_gadget_to_host(gadget) \
	(&usblb_gadget_to_bus(gadget)->host)
#define usblb_host_to_gadget(host) \
	(&usblb_host_to_bus(host)->gadget)

/************************************************/
/***************** usblb_glue.c *****************/
/************************************************/

enum usblb_event {
	USBLB_E_CONNECT,
	USBLB_E_DISCONNECT,
	USBLB_E_RESET,
};

extern void __usblb_spawn_event(struct usblb_bus *, enum usblb_event);
#define usblb_gadget_spawn_event(gadget, event) \
	__usblb_spawn_event(usblb_gadget_to_bus(gadget), event)
#define usblb_host_spawn_event(host, event) \
	__usblb_spawn_event(usblb_host_to_bus(host), event)

#define USBLB_TRANSFER_INTERVAL_MSEC 10
#define USBLB_TRANSFER_INTERVAL_JIFFIES \
	msecs_to_jiffies(USBLB_TRANSFER_INTERVAL_MSEC)
extern void usblb_glue_transfer_timer_func(unsigned long);
extern void usblb_glue_cleanup_queues(struct usblb_bus *);

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
	usblb_bus_unlock_irqrestore(usblb_gadget_to_bus(gadget), flags)

#endif /* _USBLB_H */
