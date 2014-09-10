#include <linux/netdevice.h>

struct virtnet_backend_ops {
	int (*init)(void);
	void (*exit)(void);
	int (*dev_init)(void *, unsigned int);
	void (*dev_uninit)(void *);
	int (*xmit)(struct net_device *, struct sk_buff *);
	size_t priv_size;
};

/* virtnet_net exported symbols */
extern const char DRIVER_NAME[];
extern int virtnet_recv(struct net_device *, const char *, size_t);

/* backends */
extern struct virtnet_backend_ops virtnet_lb_backend_ops;

#define virtnet_get_backend(name) \
({ \
	struct virtnet_backend_ops *backend = NULL; \
	if (!strcmp(name, "lb")) \
		backend = &virtnet_lb_backend_ops; \
	else \
		printk(KERN_ERR "%s: unknown backend %s\n", DRIVER_NAME, name); \
	backend; \
})
