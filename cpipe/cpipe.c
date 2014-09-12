#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include "cpipe_ioctl.h"

#define CPIPE_MAGIC_FIRST_MINOR 0

static const char DRIVER_NAME[] = "cpipe";

typedef STRUCT_KFIFO_PTR(char) cpipe_fifo_t;

struct cpipe_dev {
	cpipe_fifo_t rfifo;
	struct mutex rmutex; /* protects rbuf */
	wait_queue_head_t rq, wq;
	struct cdev cdev;
	struct device *dev;
	struct cpipe_dev *twin;
};

#define cpipe_dev_devt(cpdev) ((cpdev)->cdev.dev)
#define cpipe_dev_kobj(cpdev) (&(cpdev)->dev->kobj)
#define cpipe_dev_name(cpdev) (cpipe_dev_kobj(cpdev)->name)
#define cpipe_dev_twin(cpdev) ((cpdev)->twin)
#define cpipe_dev_wfifo(cpdev) (&cpipe_dev_twin(cpdev)->rfifo)
#define cpipe_dev_wmutex(cpdev) (&cpipe_dev_twin(cpdev)->rmutex)

struct cpipe_pair {
	struct cpipe_dev devices[2];
};

static int cpipe_npipes = 1;
module_param_named(npipes, cpipe_npipes, int, 0444);
MODULE_PARM_DESC(npipes, "number of pipes to create");

static int cpipe_bsize = PAGE_SIZE;
module_param_named(bsize, cpipe_bsize, int, 0444);
MODULE_PARM_DESC(bsize, "size (in bytes) of each buffer");

static int __init cpipe_check_module_params(void) {
	int err = 0;
	if (cpipe_npipes < 0) {
		printk(KERN_ERR "%s: cpipe_npipes < 0. value = %d\n",
				DRIVER_NAME, cpipe_npipes);
		err = -EINVAL;
	}
	if (cpipe_bsize < 0) {
		printk(KERN_ERR "%s: cpipe_bsize < 0. value = 0x%x\n",
				DRIVER_NAME, cpipe_bsize);
		err = -EINVAL;
	}
	/* cpipe_bsize must be a power of two */
	if (cpipe_bsize & (cpipe_bsize -1)) {
		printk(KERN_ERR "%s: cpipe_bsize is not a power of two. value = %d\n",
				DRIVER_NAME, cpipe_bsize);
		err = -EINVAL;
	}
	return err;
}

static struct cpipe_pair *cpipe_pairs;
static dev_t cpipe_dev_base;
static struct class *cpipe_class;
static const char cpipe_twin_link_name[] = "twin";

static int cpipe_mutex_lock(struct mutex *mutex, int f_flags)
{
	if ((f_flags & O_NONBLOCK) == O_NONBLOCK) {
		if (!mutex_trylock(mutex))
			return -EAGAIN;
	} else if (mutex_lock_interruptible(mutex))
			return -ERESTARTSYS;
	return 0;
}

static ssize_t cpipe_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cpipe_dev *dev = filp->private_data;
	cpipe_fifo_t *fifo = &dev->rfifo;
	struct mutex *mutex = &dev->rmutex;
	ssize_t ret;
	unsigned int copied;
again:
	ret = cpipe_mutex_lock(mutex, filp->f_flags);
	if (ret)
		return ret;
	ret = kfifo_to_user(fifo, buf, count, &copied);
	if (ret)
		goto out;
	if (!copied) {
		if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		} else {
			mutex_unlock(mutex);
			/* NOTE
			 * can't have condition !kfifo_is_empty(fifo)
			 * since we don't hold the mutex
			 */
			if (wait_event_interruptible(dev->rq, true))
				return -ERESTARTSYS;
			goto again;
		}
	}
	/* some data was read, wake up writers waiting on this pipe */
	wake_up_interruptible(&dev->twin->wq);
	*ppos += copied;
	ret = copied;
out:
	mutex_unlock(mutex);
	return ret;
}

static ssize_t cpipe_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cpipe_dev *dev = filp->private_data;
	cpipe_fifo_t *fifo = cpipe_dev_wfifo(dev);
	struct mutex *mutex = cpipe_dev_wmutex(dev);
	ssize_t ret;
	unsigned int copied;
again:
	ret = cpipe_mutex_lock(mutex, filp->f_flags);
	if (ret)
		return ret;
	ret = kfifo_from_user(fifo, buf, count, &copied);
	if (ret)
		goto out;
	if (!copied) {
		if ((filp->f_flags & O_NONBLOCK) == O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		} else {
			mutex_unlock(mutex);
			/* NOTE
			 * can't have condition !kfifo_is_full(fifo)
			 * since we don't hold the mutex
			 */
			if (wait_event_interruptible(dev->wq, true))
				return -ERESTARTSYS;
			goto again;
		}
	}
	/* some data was read, wake up readers waiting on this pipe */
	wake_up_interruptible(&dev->twin->rq);
	*ppos += copied;
	ret = copied;
out:
	mutex_unlock(mutex);
	return ret;
}

static unsigned int cpipe_poll(struct file *filp, poll_table *wait)
{
	struct cpipe_dev *dev = filp->private_data;
	unsigned int mask = 0;
	/* read */
	mutex_lock(&dev->rmutex);
	if (!kfifo_is_empty(&dev->rfifo))
		mask |= POLLIN | POLLRDNORM;
	mutex_unlock(&dev->rmutex);
	poll_wait(filp, &dev->rq, wait);
	/* write */
	mutex_lock(cpipe_dev_wmutex(dev));
	if (!kfifo_is_full(cpipe_dev_wfifo(dev)))
		mask |= POLLOUT | POLLWRNORM;
	mutex_unlock(cpipe_dev_wmutex(dev));
	poll_wait(filp, &dev->wq, wait);
	return mask;
}

/*
 * get avaliable read size
 * called with the mutex locked
 * kfifo_len is a macro and can't be addressed
 */
static int cpipe_fifo_len(cpipe_fifo_t *fifo)
{
	return kfifo_len(fifo);
}

/*
 * get avaliable write write
 * called with the mutex locked
 * kfifo_avail is a macro and can't be addressed
 */
static int cpipe_fifo_avail(cpipe_fifo_t *fifo)
{
	return kfifo_avail(fifo);
}

static int cpipe_ioctl_IOCGAVAILXX(struct mutex *mutex, cpipe_fifo_t *fifo,
	int (*get_availxx)(cpipe_fifo_t *), int f_flags, int __user *ret)
{
	int err;
	err = cpipe_mutex_lock(mutex, f_flags);
	if (err)
		return err;
	err = put_user(get_availxx(fifo), ret);
	/* not checking err since there's nothing else to do before returning it */
	mutex_unlock(mutex);
	return err;
}

static long cpipe_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cpipe_dev *dev = filp->private_data;
	long ret = 0;
	if ((_IOC_TYPE(cmd) != CPIPE_IOC_MAGIC) ||
			(_IOC_NR(cmd) > CPIPE_IOC_MAXNR))
		return -ENOTTY;
	switch (cmd) {
	case CPIPE_IOCGAVAILRD:
		ret = cpipe_ioctl_IOCGAVAILXX(&dev->rmutex, &dev->rfifo,
				cpipe_fifo_len, filp->f_flags, (int __user *)arg);
		break;
	case CPIPE_IOCGAVAILWR:
		ret = cpipe_ioctl_IOCGAVAILXX(cpipe_dev_wmutex(dev),
				cpipe_dev_wfifo(dev), cpipe_fifo_avail,
				filp->f_flags, (int __user *)arg);
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}

static int cpipe_open(struct inode *inode, struct file *filp)
{
	struct cpipe_dev *dev = container_of(inode->i_cdev,
			struct cpipe_dev, cdev);
	filp->private_data = dev;
	return 0;
}

static int cpipe_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static struct file_operations cpipe_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = cpipe_read,
	.write = cpipe_write,
	.poll = cpipe_poll,
	.unlocked_ioctl = cpipe_ioctl,
	.open = cpipe_open,
	.release = cpipe_release,
};

static int __init cpipe_dev_init(struct cpipe_dev *dev, int i, int j)
{
	int err;
	dev_t devno = MKDEV(MAJOR(cpipe_dev_base), i * 2 + j);
	err = kfifo_alloc(&dev->rfifo, cpipe_bsize, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "%s: kfifo_alloc failed i=%d j=%d err=%d\n",
				DRIVER_NAME, i, j, err);
		goto fail_kfifo_alloc;
	}
	mutex_init(&dev->rmutex);
	init_waitqueue_head(&dev->rq);
	init_waitqueue_head(&dev->wq);
	cdev_init(&dev->cdev, &cpipe_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "%s: cdev_add failed i=%d j=%d err=%d\n",
				DRIVER_NAME, i, j, err);
		goto fail_cdev_add;
	}
	dev->dev = device_create(cpipe_class, NULL, devno, dev,
			"%s%d.%d", DRIVER_NAME, i, j);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		printk(KERN_ERR "%s: device_create failed i=%d j=%d err=%d\n",
				DRIVER_NAME, i, j, err);
		goto fail_device_create;
	}
	printk(KERN_INFO "%s: created device %s successfully\n",
			DRIVER_NAME, cpipe_dev_name(dev));
	return 0;
fail_device_create:
	cdev_del(&dev->cdev);
fail_cdev_add:
	kfifo_free(&dev->rfifo);
fail_kfifo_alloc:
	return err;
}

static void cpipe_dev_destroy(struct cpipe_dev *dev)
{
	printk(KERN_INFO "%s: destroying device %s\n", DRIVER_NAME, cpipe_dev_name(dev));
	device_destroy(cpipe_class, cpipe_dev_devt(dev));
	cdev_del(&dev->cdev);
	kfifo_free(&dev->rfifo);
}

static int __init cpipe_pair_init(struct cpipe_pair *pair, int i)
{
	int err;
	int j;
	for (j = 0; j < ARRAY_SIZE(pair->devices); j++) {
		err = cpipe_dev_init(&pair->devices[j], i, j);
		if (err) {
			printk(KERN_ERR "%s: cpipe_dev_init failed i=%d j=%d err=%d\n",
					DRIVER_NAME, i, j, err);
			goto fail_cpipe_dev_init;
		}
	}
	pair->devices[0].twin = &pair->devices[1];
	pair->devices[1].twin = &pair->devices[0];
	for (j = 0; j < ARRAY_SIZE(pair->devices); j++) {
		err = sysfs_create_link(cpipe_dev_kobj(&pair->devices[j]),
				cpipe_dev_kobj(pair->devices[j].twin),
				cpipe_twin_link_name);
		if (err) {
			printk(KERN_ERR "%s: sysfs_create_link failed "
					"i=%d j=%d err=%d\n",
					DRIVER_NAME, i, j, err);
			goto fail_sysfs_create_link;
		}
	}
	printk(KERN_INFO "%s: created pair %s%d successfully\n",
			DRIVER_NAME, DRIVER_NAME, i);
	return 0;
fail_sysfs_create_link:
	while (j--)
		sysfs_remove_link(cpipe_dev_kobj(&pair->devices[j]),
				cpipe_twin_link_name);
	j = ARRAY_SIZE(pair->devices);
fail_cpipe_dev_init:
	while (j--)
		cpipe_dev_destroy(&pair->devices[j]);
	return err;
}

static void cpipe_pair_destroy(struct cpipe_pair *pair)
{
	int i, j;
	i = MINOR(cpipe_dev_devt(&pair->devices[0])) / 2;
	printk(KERN_INFO "%s: destroying pair %s%d\n",
			DRIVER_NAME, DRIVER_NAME, i);
	for (j = 0; j < ARRAY_SIZE(pair->devices); j++) {
		sysfs_remove_link(cpipe_dev_kobj(&pair->devices[j]),
				cpipe_twin_link_name);
		cpipe_dev_destroy(&pair->devices[j]);
	}
}

static int __init cpipe_init(void)
{
	int err;
	int i;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	err = cpipe_check_module_params();
	if (err)
		return err;
	cpipe_pairs = vmalloc(sizeof(cpipe_pairs[0]) * cpipe_npipes);
	if (!cpipe_pairs) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate cpipe_pairs\n", DRIVER_NAME);
		goto fail_vmalloc_cpipe_pairs;
	}
	memset(cpipe_pairs, 0, sizeof(cpipe_pairs[0]) * cpipe_npipes);
	err = alloc_chrdev_region(&cpipe_dev_base, CPIPE_MAGIC_FIRST_MINOR,
			cpipe_npipes * 2, DRIVER_NAME);
	if (err) {
		printk(KERN_ERR "%s: alloc_chrdev_region failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_alloc_chrdev_region;
	}
	cpipe_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(cpipe_class)) {
		err = PTR_ERR(cpipe_class);
		printk(KERN_ERR "%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}
	for (i = 0; i < cpipe_npipes; i++) {
		err = cpipe_pair_init(&cpipe_pairs[i], i);
		if (err) {
			printk(KERN_ERR "%s: cpipe_pair_init failed. i = %d, "
					"err = %d\n", DRIVER_NAME, i, err);
			goto fail_cpipe_pair_init_loop;
		}
	}
	printk(KERN_INFO "%s: initializated successfully\n", DRIVER_NAME);
	return 0;
fail_cpipe_pair_init_loop:
	while (i--)
		cpipe_pair_destroy(&cpipe_pairs[i]);
	class_destroy(cpipe_class);
fail_class_create:
	unregister_chrdev_region(cpipe_dev_base, cpipe_npipes * 2);
fail_alloc_chrdev_region:
	vfree(cpipe_pairs);
fail_vmalloc_cpipe_pairs:
	return err;
}
module_init(cpipe_init);

static void __exit cpipe_exit(void)
{
	int i;
	for (i = 0; i < cpipe_npipes; i++)
		cpipe_pair_destroy(&cpipe_pairs[i]);
	class_destroy(cpipe_class);
	unregister_chrdev_region(cpipe_dev_base, cpipe_npipes * 2);
	vfree(cpipe_pairs);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(cpipe_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Pairs of char devices acting as pipes");
MODULE_VERSION("1.0.1");
MODULE_LICENSE("GPL");

