#define pr_fmt(fmt) KBUILD_BASENAME ": " fmt

#include <linux/poll.h>

#include "virtnet.h"

struct virtnet_chr_dev {
	struct list_head packets;
	spinlock_t lock;
	wait_queue_head_t waitq;
	struct device *dev;
};
#define virtnet_chr_dev_devt(vcdev) ((vcdev)->dev->devt)
#define virtnet_chr_dev_to_netdev(vcdev) \
	(((void *)vcdev) - ALIGN(sizeof(struct net_device), NETDEV_ALIGN))

struct virtnet_chr_packet {
	char *data;
	size_t len;
	struct list_head link;
};

static int virtnet_chr_major;
static unsigned int virtnet_chr_ndev;
static struct class *virtnet_chr_class;

static struct virtnet_chr_packet *virtnet_chr_get_next_packet(
		struct virtnet_chr_dev *vcdev, int block)
{
	struct virtnet_chr_packet *packet = NULL;
	unsigned long flags;
	DEFINE_WAIT(wait);

	while (!packet) {
		if (block)
			prepare_to_wait(&vcdev->waitq, &wait,
					TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&vcdev->lock, flags);
		if (list_empty(&vcdev->packets)) {
			if (block) {
				spin_unlock_irqrestore(&vcdev->lock, flags);
				schedule();
				if (signal_pending(current))
					packet = ERR_PTR(-ERESTARTSYS);
				spin_lock_irqsave(&vcdev->lock, flags);
			} else {
				packet = ERR_PTR(-EAGAIN);
			}
		} else {
			packet = list_first_entry(&vcdev->packets,
					struct virtnet_chr_packet, link);
			list_del(&packet->link);
		}
		spin_unlock_irqrestore(&vcdev->lock, flags);
		if (block)
			finish_wait(&vcdev->waitq, &wait);
	}
	return packet;
}

static ssize_t virtnet_chr_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct virtnet_chr_dev *vcdev = filp->private_data;
	struct virtnet_chr_packet *packet;
	ssize_t ret;

	packet = virtnet_chr_get_next_packet(vcdev,
			!(filp->f_flags & O_NONBLOCK));
	if (IS_ERR(packet))
		return PTR_ERR(packet);

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

/* unexported. taken from drivers/base/core.c */
static int __match_devt(struct device *dev, const void *data)
{
	const dev_t *devt = data;

	return dev->devt == *devt;
}

static int virtnet_chr_open(struct inode *inode, struct file *filp)
{
	struct virtnet_chr_dev *vcdev = dev_get_drvdata(
			class_find_device(virtnet_chr_class, NULL,
					&inode->i_rdev, __match_devt));
	filp->private_data = vcdev;
	return 0;
}

static int virtnet_chr_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations virtnet_chr_fops = {
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
	if (!packet)
		return -ENOMEM;

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
	dev_t devno = MKDEV(virtnet_chr_major, minor);

	spin_lock_init(&vcdev->lock);
	init_waitqueue_head(&vcdev->waitq);
	INIT_LIST_HEAD(&vcdev->packets);

	vcdev->dev = device_create(virtnet_chr_class, NULL, devno, vcdev,
			"%s%d", KBUILD_BASENAME, minor);
	if (IS_ERR(vcdev->dev)) {
		err = PTR_ERR(vcdev->dev);
		pr_err("device_create failed minor=%d err=%d\n", minor, err);
		goto fail_device_create;
	}

	return 0;

fail_device_create:
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
}

static int virtnet_chr_init(unsigned int nifaces)
{
	int err;

	virtnet_chr_ndev = nifaces;

	virtnet_chr_major = __register_chrdev(0, 0, virtnet_chr_ndev,
			KBUILD_BASENAME, &virtnet_chr_fops);
	if (virtnet_chr_major < 0) {
		err = virtnet_chr_major;
		pr_err("__register_chrdev failed. err = %d\n", err);
		goto fail_register_chrdev;
	}

	virtnet_chr_class = class_create(THIS_MODULE, KBUILD_BASENAME);
	if (IS_ERR(virtnet_chr_class)) {
		err = PTR_ERR(virtnet_chr_class);
		pr_err("class_create failed. err = %d\n", err);
		goto fail_class_create;
	}

	return 0;

fail_class_create:
	__unregister_chrdev(virtnet_chr_major, 0, virtnet_chr_ndev,
			KBUILD_BASENAME);
fail_register_chrdev:
	return err;
}

static void virtnet_chr_exit(void)
{
	class_destroy(virtnet_chr_class);
	__unregister_chrdev(virtnet_chr_major, 0, virtnet_chr_ndev,
			KBUILD_BASENAME);
}

DEFINE_VIRTNET_BACKEND(chr,
	.init = virtnet_chr_init,
	.exit = virtnet_chr_exit,
	.dev_init = virtnet_chr_dev_init,
	.dev_uninit = virtnet_chr_dev_uninit,
	.xmit = virtnet_chr_xmit,
	.priv_size = sizeof(struct virtnet_chr_dev)
);
