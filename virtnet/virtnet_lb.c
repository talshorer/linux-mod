#include "virtnet.h"

/* one second */
#define VIRTNET_LB_DELAY_JIFFIES (1 * HZ)

struct virtnet_lb_dev {
	struct list_head entries;
	spinlock_t lock;
};

struct virtnet_lb_entry {
	struct timer_list timer;
	struct net_device *dev;
	char *data;
	size_t len;
	struct list_head link;
};

static void virtnet_lb_timer_func(unsigned long data)
{
	struct virtnet_lb_entry *lbe = (struct virtnet_lb_entry *)data;
	struct virtnet_lb_dev *lbdev = netdev_priv(lbe->dev);
	unsigned long flags;
	virtnet_recv(lbe->dev, lbe->data, lbe->len);
	spin_lock_irqsave(&lbdev->lock, flags);
	list_del(&lbe->link);
	kfree(lbe);
	spin_unlock_irqrestore(&lbdev->lock, flags);
}

static  int virtnet_lb_xmit(struct net_device *dev, struct sk_buff *skb)
{
	struct virtnet_lb_dev *lbdev = netdev_priv(dev);
	struct virtnet_lb_entry *lbe;
	unsigned long flags;
	lbe = kzalloc(sizeof(*lbe) + skb->len, GFP_ATOMIC);
	if (!lbe) {
		printk(KERN_ERR "%s: <%s> failed to allocate entry\n", DRIVER_NAME, __func__);
		return -ENOMEM;
	}

	lbe->dev = dev;
	INIT_LIST_HEAD(&lbe->link);

	lbe->data = (void *)(lbe + 1);
	lbe->len = skb->len;
	memcpy(lbe->data, skb->data, skb->len);

	setup_timer(&lbe->timer, virtnet_lb_timer_func, (unsigned long)lbe);
	spin_lock_irqsave(&lbdev->lock, flags);
	list_add(&lbe->link, &lbdev->entries);
	mod_timer(&lbe->timer, jiffies + VIRTNET_LB_DELAY_JIFFIES);
	spin_unlock_irqrestore(&lbdev->lock, flags);

	return 0;
}

int virtnet_lb_dev_init(void *priv, unsigned int minor)
{
	struct virtnet_lb_dev *lbdev = priv;
	spin_lock_init(&lbdev->lock);
	INIT_LIST_HEAD(&lbdev->entries);
	return 0;
}

void virtnet_lb_dev_uninit(void *priv)
{
	struct virtnet_lb_dev *lbdev = priv;
	struct virtnet_lb_entry *lbe, *tmp;
	unsigned long flags;
	spin_lock_irqsave(&lbdev->lock, flags);
	list_for_each_entry_safe(lbe, tmp, &lbdev->entries, link) {
		/*
		 * if it's active, it's not running.
		 * if it's running, let it kfree itself.
		 */
		if (del_timer(&lbe->timer))
			kfree(lbe);
		/* give timers a chance to run on another processor */
		spin_unlock_irqrestore(&lbdev->lock, flags);
		udelay(jiffies_to_usecs(1));
		spin_lock_irqsave(&lbdev->lock, flags);
	}
	spin_unlock_irqrestore(&lbdev->lock, flags);
}

struct virtnet_backend_ops virtnet_lb_backend_ops = {
	.dev_init = virtnet_lb_dev_init,
	.dev_uninit = virtnet_lb_dev_uninit,
	.xmit = virtnet_lb_xmit,
	.priv_size = sizeof(struct virtnet_lb_dev),
};
