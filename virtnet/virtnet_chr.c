#include <linux/cdev.h>
#include <linux/poll.h>

#include "virtnet.h"

#define VIRTNET_CHR_MAGIC_FIRST_MINOR 0

static const char DRIVER_NAME[] = "virtnet_chr";

struct virtnet_chr_dev {
	struct cdev cdev;
	struct list_head packets;
	spinlock_t lock;
	wait_queue_head_t waitq;
	struct device *dev;
};
#define virtnet_chr_dev_devt(vcdev) ((vcdev)->cdev.dev)
#define virtnet_chr_dev_to_netdev(vcdev) \
		(((void *)vcdev) - ALIGN(sizeof(struct net_device), NETDEV_ALIGN))

struct virtnet_chr_packet {
	char *data;
	size_t len;
	struct list_head link;
};

static dev_t virtnet_chr_dev_base;
static unsigned int virtnet_chr_ndev;
static struct class *virtnet_chr_class;


static ssize_t virtnet_chr_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct virtnet_chr_dev *vcdev = filp->private_data;
	struct virtnet_chr_packet *packet;
	ssize_t ret;
	unsigned long flags;

	spin_lock_irqsave(&vcdev->lock, flags);
	while (list_empty(&vcdev->packets)) {
		spin_unlock_irqrestore(&vcdev->lock, flags);
		if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK)
			return -EAGAIN;
		/* NOTE
		 * can't have condition !list_empty(&vcdev->packets)
		 * since we don't hold the lock
		 */
		else if (wait_event_interruptible(vcdev->waitq, true))
			return -ERESTARTSYS;
		spin_lock_irqsave(&vcdev->lock, flags);
	}
	packet = list_first_entry(&vcdev->packets,
			struct virtnet_chr_packet, link);
	list_del(&packet->link);
	spin_unlock_irqrestore(&vcdev->lock, flags);

	count = min(count, packet->len);

	if (copy_to_user(buf, packet->data, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = count;

out:
	kfree(packet);
	return count;
}

static ssize_t virtnet_chr_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct virtnet_chr_dev *vcdev = filp->private_data;
	ssize_t ret;
	char *kbuf;
	gfp_t gfp_flags;

	if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK)
		gfp_flags = GFP_ATOMIC;
	else
		gfp_flags = GFP_KERNEL;
	kbuf = kmalloc(count, gfp_flags);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = virtnet_recv(virtnet_chr_dev_to_netdev(vcdev), kbuf, count);
	if (ret)
		goto out;

	ret = count;

out:
	kfree(kbuf);
	return ret;
}

static unsigned int virtnet_chr_poll(struct file *filp, poll_table *wait)
{
	struct virtnet_chr_dev *vcdev = filp->private_data;
	unsigned int mask = 0;
	unsigned long flags;

	/* always writable */
	mask |= POLLOUT | POLLWRNORM;

	/* readable when the packet list is not empty */
	spin_lock_irqsave(&vcdev->lock, flags);
	if (!list_empty(&vcdev->packets))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&vcdev->lock, flags);
	poll_wait(filp, &vcdev->waitq, wait);

	return mask;
}

static int virtnet_chr_open(struct inode *inode, struct file *filp)
{
	struct virtnet_chr_dev *vcdev = container_of(inode->i_cdev,
			struct virtnet_chr_dev, cdev);
	filp->private_data = vcdev;
	return 0;
}

static int virtnet_chr_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static struct file_operations virtnet_chr_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = virtnet_chr_read,
	.write = virtnet_chr_write,
	.poll = virtnet_chr_poll,
	.open = virtnet_chr_open,
	.release = virtnet_chr_release,
};

static int virtnet_chr_xmit(struct net_device *dev,
		const char *buf, size_t len)
{
	struct virtnet_chr_dev *vcdev = netdev_priv(dev);
	struct virtnet_chr_packet *packet;
	unsigned long flags;

	packet = kzalloc(sizeof(*packet) + len, GFP_ATOMIC);
	if (!packet) {
		printk(KERN_ERR "%s: <%s> failed to allocate packet\n", DRIVER_NAME, __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&packet->link);
	packet->data = (void *)(packet + 1);
	packet->len = len;
	memcpy(packet->data, buf, len);

	spin_lock_irqsave(&vcdev->lock, flags);
	list_add_tail(&packet->link, &vcdev->packets);
	spin_unlock_irqrestore(&vcdev->lock, flags);

	wake_up_interruptible(&vcdev->waitq);

	return 0;
}

static int virtnet_chr_dev_init(void *priv, unsigned int minor)
{
	struct virtnet_chr_dev *vcdev = (struct virtnet_chr_dev *)priv;
	int err;
	dev_t devno = MKDEV(MAJOR(virtnet_chr_dev_base), minor);

	spin_lock_init(&vcdev->lock);
	init_waitqueue_head(&vcdev->waitq);
	INIT_LIST_HEAD(&vcdev->packets);

	cdev_init(&vcdev->cdev, &virtnet_chr_fops);
	vcdev->cdev.owner = THIS_MODULE;
	err = cdev_add(&vcdev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "%s: cdev_add failed minor=%d err=%d\n",
				DRIVER_NAME, minor, err);
		goto fail_cdev_add;
	}

	vcdev->dev = device_create(virtnet_chr_class, NULL, devno, vcdev,
			"%s%d", DRIVER_NAME, minor);
	if (IS_ERR(vcdev->dev)) {
		err = PTR_ERR(vcdev->dev);
		printk(KERN_ERR "%s: device_create failed minor=%d err=%d\n",
				DRIVER_NAME, minor, err);
		goto fail_device_create;
	}

	return 0;

fail_device_create:
	cdev_del(&vcdev->cdev);
fail_cdev_add:
	return err;
}

static void virtnet_chr_dev_uninit(void *priv)
{
	struct virtnet_chr_dev *vcdev = (struct virtnet_chr_dev *)priv;
	struct virtnet_chr_packet *packet, *tmp;
	unsigned long flags;
	spin_lock_irqsave(&vcdev->lock, flags);
	list_for_each_entry_safe(packet, tmp, &vcdev->packets, link)
		kfree(packet);
	spin_unlock_irqrestore(&vcdev->lock, flags);
	device_destroy(virtnet_chr_class, virtnet_chr_dev_devt(vcdev));
	cdev_del(&vcdev->cdev);
}

static int virtnet_chr_init(unsigned int nifaces)
{
	int err;

	virtnet_chr_ndev = nifaces;

	err = alloc_chrdev_region(&virtnet_chr_dev_base,
			VIRTNET_CHR_MAGIC_FIRST_MINOR, virtnet_chr_ndev, DRIVER_NAME);
	if (err) {
		printk(KERN_ERR "%s: alloc_chrdev_region failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_alloc_chrdev_region;
	}

	virtnet_chr_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(virtnet_chr_class)) {
		err = PTR_ERR(virtnet_chr_class);
		printk(KERN_ERR "%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}

	return 0;

fail_class_create:
	unregister_chrdev_region(virtnet_chr_dev_base, virtnet_chr_ndev);
fail_alloc_chrdev_region:
	return err;
}

static void virtnet_chr_exit(void)
{
	class_destroy(virtnet_chr_class);
	unregister_chrdev_region(virtnet_chr_dev_base, virtnet_chr_ndev);
}

struct virtnet_backend_ops virtnet_chr_backend_ops = {
	.init = virtnet_chr_init,
	.exit = virtnet_chr_exit,
	.dev_init = virtnet_chr_dev_init,
	.dev_uninit = virtnet_chr_dev_uninit,
	.xmit = virtnet_chr_xmit,
	.priv_size = sizeof(struct virtnet_chr_dev),
};
