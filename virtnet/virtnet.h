#ifndef _VIRTNET_H
#define _VIRTNET_H

#include <linux/netdevice.h>

#include "virtnet_backend_glue.gen.h"

struct virtnet_backend_ops {
	int (*init)(unsigned int);
	void (*exit)(void);
	int (*dev_init)(void *, unsigned int);
	void (*dev_uninit)(void *);
	int (*xmit)(struct net_device *, const char*, size_t);
	size_t priv_size;
};

/* virtnet_net exported symbols */
extern int virtnet_recv(struct net_device *, const char *, size_t);

/* virtnet_backend_glue exported symbols */
extern struct virtnet_backend_ops *virtnet_get_backend(const char *);

#endif /* _VIRTNET_H */
