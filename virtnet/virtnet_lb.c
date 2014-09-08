#include "virtnet.h"

/* one second */
#define VIRTNET_LB_DELAY_JIFFIES (1 * HZ)

struct virtnet_lb_entry {
	struct timer_list timer;
	struct net_device *dev;
	char *data;
	size_t len;
};

static void virtnet_lb_timer_func(unsigned long data)
{
	struct virtnet_lb_entry *lbe = (struct virtnet_lb_entry *)data;
	virtnet_recv(lbe->dev, lbe->data, lbe->len);
	kfree(lbe);
}

int virtnet_backend_xmit(struct net_device *dev, struct sk_buff *skb)
{
	struct virtnet_lb_entry *lbe;
	lbe = kzalloc(sizeof(*lbe) + skb->len, GFP_ATOMIC);
	if (!lbe) {
		printk(KERN_ERR "%s: <%s> failed to allocate entry\n", DRIVER_NAME, __func__);
		return -ENOMEM;
	}

	lbe->dev = dev;

	lbe->data = (void *)(lbe + 1);
	lbe->len = skb->len;
	memcpy(lbe->data, skb->data, skb->len);

	init_timer(&lbe->timer);
	lbe->timer.function = virtnet_lb_timer_func;
	lbe->timer.data = (unsigned long)lbe;
	mod_timer(&lbe->timer, jiffies + VIRTNET_LB_DELAY_JIFFIES);

	return 0;
}

int __init virtnet_backend_init(void)
{
	return 0;
}

void virtnet_backend_exit(void)
{
}

