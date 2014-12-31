#ifndef _USBLB_HOST_H
#define _USBLB_HOST_H

#include "usblb.h"

struct usblb_host_urb_node {
	struct list_head link;
	struct urb *urb;
};

#endif /* _USBLB_HOST_H */
