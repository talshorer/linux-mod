#include <linux/kfifo.h>
#include <linux/cdev.h>
#include <linux/poll.h>

#include "virtnet.h"

#define VIRTNET_CHR_MAGIC_FIRST_MINOR 0
#define VIRTNET_CHR_KFIFO_SIZE PAGE_SIZE

static const char DRIVER_NAME[] = "virtnet_chr";

typedef STRUCT_KFIFO_PTR(char) virtnet_chr_fifo_t;

struct virtnet_chr_dev {
	struct cdev cdev;
	virtnet_chr_fifo_t fifo;
	struct mutex mutex;
	wait_queue_head_t waitq;
	struct device *dev;
};

#define virtnet_chr_dev_devt(vcdev) ((vcdev)->cdev.dev)

#define virtnet_chr_dev_to_netdev(vcdev) \
		(((void *)vcdev) - ALIGN(sizeof(struct net_device), NETDEV_ALIGN))

static dev_t virtnet_chr_dev_base;
static unsigned int virtnet_chr_ndev;
static struct class *virtnet_chr_class;


static ssize_t virtnet_chr_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	/* struct virtnet_chr_dev *vcdev = filp->private_data; */
	/* ssize_t ret; */
	/* TODO */
	return 0;
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

	/* always writable */
	mask |= POLLOUT | POLLWRNORM;

	/* readable when the fifo is not empty */
	mutex_lock(&vcdev->mutex);
	if (!kfifo_is_empty(&vcdev->fifo))
		mask |= POLLIN | POLLRDNORM;
	mutex_unlock(&vcdev->mutex);
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

static int virtnet_chr_xmit(struct net_device *dev, struct sk_buff *skb)
{
	/* TODO */
	return 0;
}

static int virtnet_chr_dev_init(void *priv, unsigned int minor)
{
	struct virtnet_chr_dev *vcdev = (struct virtnet_chr_dev *)priv;
	int err;
	dev_t devno = MKDEV(MAJOR(virtnet_chr_dev_base), minor);

	mutex_init(&vcdev->mutex);
	init_waitqueue_head(&vcdev->waitq);

	err = kfifo_alloc(&vcdev->fifo, VIRTNET_CHR_KFIFO_SIZE, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "%s: kfifo_alloc failed minor=%d err=%d\n",
				DRIVER_NAME, minor, err);
		goto fail_kfifo_alloc;
	}

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
	kfifo_free(&vcdev->fifo);
fail_kfifo_alloc:
	return err;
}

static void virtnet_chr_dev_uninit(void *priv)
{
	struct virtnet_chr_dev *vcdev = (struct virtnet_chr_dev *)priv;
	device_destroy(virtnet_chr_class, virtnet_chr_dev_devt(vcdev));
	cdev_del(&vcdev->cdev);
	kfifo_free(&vcdev->fifo);
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
