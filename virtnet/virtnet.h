#ifndef _VIRTNET_H
#define _VIRTNET_H

#include <linux/netdevice.h>

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

/* backends */
extern struct virtnet_backend_ops virtnet_lb_backend_ops;
extern struct virtnet_backend_ops virtnet_chr_backend_ops;

#define virtnet_get_backend(name) \
({ \
	struct virtnet_backend_ops *backend = NULL; \
	if (!strcmp(name, "lb")) \
		backend = &virtnet_lb_backend_ops; \
	else if (!strcmp(name, "chr")) \
		backend = &virtnet_chr_backend_ops; \
	else \
		printk(KERN_ERR "%s: unknown backend %s\n", DRIVER_NAME, name); \
	backend; \
})

#endif /* _VIRTNET_H */
