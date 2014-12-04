#ifndef _USBLB_H
#define _USBLB_H

#include <linux/usb/gadget.h>

struct usblb_gadget;
struct usblb_host;

/************************************************/
/***************** usblb_main.c *****************/
/************************************************/

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

/************************************************/
/***************** usblb_host.c *****************/
/************************************************/

struct usblb_host {
	struct usblb_gadget *gadget;
	struct device *dev;
};

extern int usblb_host_init(void);
extern void usblb_host_exit(void);

extern int usblb_host_device_setup(struct usblb_host *, int);
extern void usblb_host_device_cleanup(struct usblb_host *);

extern int usblb_host_set_gadget(struct usblb_host *, struct usblb_gadget *);

#endif /* _USBLB_H */
