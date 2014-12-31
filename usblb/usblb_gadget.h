#ifndef _USBLB_GADGET_H
#define _USBLB_GADGET_H

#include "usblb.h"

#define to_usblb_gadget(_g) container_of(_g, struct usblb_gadget, g)

/* single direction */
struct usblb_gadget_ep {
	struct usb_ep ep;
	struct usblb_gadget *g;
	u8 epnum;
	char name[16];
};

#define to_usblb_gadget_ep(_ep) container_of(_ep, struct usblb_gadget_ep, ep)

struct usblb_gadget_request {
	struct usb_request req;
};

#define to_usblb_gadget_request(_req) \
	container_of(_req, struct usblb_gadget_request, req)

#endif /* _USBLB_GADGET_H */
