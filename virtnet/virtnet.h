#include <linux/netdevice.h>

#define VIRTNET_LOOPBACK

#ifdef VIRTNET_LOOPBACK
extern int virtnet_do_loopback(struct net_device *dev, struct sk_buff *skb);
#define virtnet_do_xmit virtnet_do_loopback
#endif /* VIRTNET_LOOPBACK */

extern const char DRIVER_NAME[];

extern int virtnet_recv(struct net_device *, const char *, size_t);
