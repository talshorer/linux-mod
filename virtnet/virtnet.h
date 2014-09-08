#include <linux/netdevice.h>

/* virtnet_$(BACKEND) exported symbols */
extern int virtnet_backend_xmit(struct net_device *dev, struct sk_buff *skb);
int __init virtnet_backend_init(void);
void virtnet_backend_exit(void);

/* virtnet_net exported symbols */
extern const char DRIVER_NAME[];
extern int virtnet_recv(struct net_device *, const char *, size_t);
