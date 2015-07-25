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

#define VIRTNET_BACKEND(name) virtnet_##name##_backend_ops

#include "virtnet_backend_glue.gen.h"

#define DEFINE_VIRTNET_BACKEND(name, ...) \
	struct virtnet_backend_ops VIRTNET_BACKEND(name) = { __VA_ARGS__ }


/* generate an extern directive for each backend */
#define __EMPTY /* prevent checkpatch from complaining about the semicolon */
#define VIRTNET_BACKEND_ENTRY(name) \
	extern struct virtnet_backend_ops VIRTNET_BACKEND(name); __EMPTY
VIRTNET_BACKEND_GLUE()
#undef VIRTNET_BACKEND_ENTRY
#undef __EMPTY

/* virtnet_net exported symbols */
extern int virtnet_recv(struct net_device *, const char *, size_t);

/* virtnet_backend_glue exported symbols */
extern struct virtnet_backend_ops *virtnet_get_backend(const char *);

#endif /* _VIRTNET_H */
