#include "virtnet.h"

/* one second */
#define VIRTNET_LB_DELAY_JIFFIES (1 * HZ)

static const char DRIVER_NAME[] = "virtnet_lb";

struct virtnet_lb_dev {
	struct list_head entries;
	spinlock_t lock;
	atomic_t allocated;
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
	struct virtnet_lb_entry *entry = (struct virtnet_lb_entry *)data;
	struct virtnet_lb_dev *lbdev = netdev_priv(entry->dev);
	unsigned long flags;

	virtnet_recv(entry->dev, entry->data, entry->len);
	spin_lock_irqsave(&lbdev->lock, flags);
	list_del(&entry->link);
	kfree(entry);
	atomic_dec(&lbdev->allocated);
	spin_unlock_irqrestore(&lbdev->lock, flags);
}

static int virtnet_lb_xmit(struct net_device *dev, const char *buf, size_t len)
{
	struct virtnet_lb_dev *lbdev = netdev_priv(dev);
	struct virtnet_lb_entry *entry;
	unsigned long flags;

	entry = kzalloc(sizeof(*entry) + len, GFP_ATOMIC);
	if (!entry) {
		pr_err("%s: <%s> failed to allocate entry\n",
				DRIVER_NAME, __func__);
		return -ENOMEM;
	}
	atomic_inc(&lbdev->allocated);

	entry->dev = dev;
	INIT_LIST_HEAD(&entry->link);

	entry->data = (void *)(entry + 1);
	entry->len = len;
	memcpy(entry->data, buf, len);

	setup_timer(&entry->timer, virtnet_lb_timer_func,
			(unsigned long)entry);
	spin_lock_irqsave(&lbdev->lock, flags);
	list_add(&entry->link, &lbdev->entries);
	mod_timer(&entry->timer, jiffies + VIRTNET_LB_DELAY_JIFFIES);
	spin_unlock_irqrestore(&lbdev->lock, flags);

	return 0;
}

static int virtnet_lb_dev_init(void *priv, unsigned int minor)
{
	struct virtnet_lb_dev *lbdev = priv;

	spin_lock_init(&lbdev->lock);
	INIT_LIST_HEAD(&lbdev->entries);
	atomic_set(&lbdev->allocated, 0);
	return 0;
}

static void virtnet_lb_dev_uninit(void *priv)
{
	struct virtnet_lb_dev *lbdev = priv;
	struct virtnet_lb_entry *entry, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&lbdev->lock, flags);
	list_for_each_entry_safe(entry, tmp, &lbdev->entries, link) {
		spin_unlock_irqrestore(&lbdev->lock, flags);
		/*
		 * if it's active, it's not running.
		 * if it's running, let it kfree itself.
		 */
		if (del_timer_sync(&entry->timer)) {
			kfree(entry);
			atomic_dec(&lbdev->allocated);
		}
		spin_lock_irqsave(&lbdev->lock, flags);
	}
	spin_unlock_irqrestore(&lbdev->lock, flags);
	/*
	 * if not smp, all timers were deleted by the loop.
	 * else, wait for any that are still running to finish on other
	 * processors.
	 */
	while (atomic_read(&lbdev->allocated))
		/* do nothing */;
}

struct virtnet_backend_ops virtnet_lb_backend_ops = {
	.dev_init = virtnet_lb_dev_init,
	.dev_uninit = virtnet_lb_dev_uninit,
	.xmit = virtnet_lb_xmit,
	.priv_size = sizeof(struct virtnet_lb_dev),
};
