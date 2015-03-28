#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include <lmod/meta.h>

#include "cpipe_ioctl.h"

static const char DRIVER_NAME[] = "cpipe";

typedef STRUCT_KFIFO_PTR(char) cpipe_fifo_t;

struct cpipe_dev {
	cpipe_fifo_t rfifo;
	struct mutex rmutex; /* protects rbuf */
	wait_queue_head_t rq, wq;
	struct device *dev;
	struct cpipe_dev *twin;
};

#define cpipe_dev_devt(cpdev) ((cpdev)->dev->devt)
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
		pr_err("%s: cpipe_npipes < 0. value = %d\n",
				DRIVER_NAME, cpipe_npipes);
		err = -EINVAL;
	}
	if (cpipe_bsize < 0) {
		pr_err("%s: cpipe_bsize < 0. value = 0x%x\n",
				DRIVER_NAME, cpipe_bsize);
		err = -EINVAL;
	}
	/* cpipe_bsize must be a power of two */
	if (cpipe_bsize & (cpipe_bsize -1)) {
		pr_err("%s: cpipe_bsize is not a power of two. "
				"value = %d\n", DRIVER_NAME, cpipe_bsize);
		err = -EINVAL;
	}
	return err;
}

static struct cpipe_pair *cpipe_pairs;
static int cpipe_major;
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

/*
 * it's assumed the mutex is needed for the condition.
 * release it either way to simplify code.
 */
#define cpipe_wait(waitq, mutex, sleep_cond)             \
({                                                       \
	int __ret = 0;                                       \
	wait_queue_head_t *__waitq = (waitq);                \
	DEFINE_WAIT(wait);                                   \
	prepare_to_wait(__waitq, &wait, TASK_INTERRUPTIBLE); \
	if (sleep_cond) {                                    \
		mutex_unlock(mutex);                             \
		schedule();                                      \
	} else mutex_unlock(mutex);                          \
	finish_wait(__waitq, &wait);                         \
	if (signal_pending(current))                         \
		__ret = -ERESTARTSYS;                            \
	__ret;                                               \
})

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
			ret = cpipe_wait(&dev->rq, mutex,
					kfifo_is_empty(fifo));
			/* cpipe_wait unlocks the mutex */
			if (ret)
				return ret;
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
			ret = cpipe_wait(&dev->wq, mutex, kfifo_is_full(fifo));
			/* cpipe_wait unlocks the mutex */
			if (ret)
				return ret;
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
	if (mutex_trylock(&dev->rmutex)) {
		if (!kfifo_is_empty(&dev->rfifo))
			mask |= POLLIN | POLLRDNORM;
		mutex_unlock(&dev->rmutex);
	}
	poll_wait(filp, &dev->rq, wait);
	/* write */
	if (mutex_trylock(cpipe_dev_wmutex(dev))) {
		if (!kfifo_is_full(cpipe_dev_wfifo(dev)))
			mask |= POLLOUT | POLLWRNORM;
		mutex_unlock(cpipe_dev_wmutex(dev));
	}
	poll_wait(filp, &dev->wq, wait);
	return mask;
}

static int cpipe_ioctl_IOCGAVAILXX(struct mutex *mutex, cpipe_fifo_t *fifo,
	int (*get_availxx)(cpipe_fifo_t *), int f_flags, int __user *ret)
{
	int err;

	err = cpipe_mutex_lock(mutex, f_flags);
	if (err)
		return err;
	err = put_user(get_availxx(fifo), ret);
	/* not checking err since there's nothing to do before returning it */
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
		ret = cpipe_ioctl_IOCGAVAILXX(&dev->rmutex,
				&dev->rfifo, cpipe_fifo_len,
				filp->f_flags, (int __user *)arg);
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
	unsigned int minor = iminor(inode);
	struct cpipe_dev *dev = &cpipe_pairs[minor >> 1].devices[minor & 1];

	filp->private_data = dev;
	return 0;
}

static int cpipe_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations cpipe_fops = {
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
	dev_t devno = MKDEV(cpipe_major, i * 2 + j);

	err = kfifo_alloc(&dev->rfifo, cpipe_bsize, GFP_KERNEL);
	if (err) {
		pr_err("%s: kfifo_alloc failed i=%d j=%d err=%d\n",
				DRIVER_NAME, i, j, err);
		goto fail_kfifo_alloc;
	}
	mutex_init(&dev->rmutex);
	init_waitqueue_head(&dev->rq);
	init_waitqueue_head(&dev->wq);
	dev->dev = device_create(cpipe_class, NULL, devno, dev,
			"%s%d.%d", DRIVER_NAME, i, j);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		pr_err("%s: device_create failed i=%d j=%d err=%d\n",
				DRIVER_NAME, i, j, err);
		goto fail_device_create;
	}
	pr_info("%s: created device %s successfully\n",
			DRIVER_NAME, cpipe_dev_name(dev));
	return 0;
fail_device_create:
	kfifo_free(&dev->rfifo);
fail_kfifo_alloc:
	return err;
}

static void cpipe_dev_destroy(struct cpipe_dev *dev)
{
	pr_info("%s: destroying device %s\n", DRIVER_NAME,
			cpipe_dev_name(dev));
	device_destroy(cpipe_class, cpipe_dev_devt(dev));
	kfifo_free(&dev->rfifo);
}

static int __init cpipe_pair_init(struct cpipe_pair *pair, int i)
{
	int err;
	int j;

	for (j = 0; j < ARRAY_SIZE(pair->devices); j++) {
		err = cpipe_dev_init(&pair->devices[j], i, j);
		if (err) {
			pr_err("%s: cpipe_dev_init failed "
					"i=%d j=%d err=%d\n",
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
			pr_err("%s: sysfs_create_link failed "
					"i=%d j=%d err=%d\n",
					DRIVER_NAME, i, j, err);
			goto fail_sysfs_create_link;
		}
	}
	pr_info("%s: created pair %s%d successfully\n",
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
	pr_info("%s: destroying pair %s%d\n",
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

	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	err = cpipe_check_module_params();
	if (err)
		return err;
	cpipe_pairs = vmalloc(sizeof(cpipe_pairs[0]) * cpipe_npipes);
	if (!cpipe_pairs) {
		err = -ENOMEM;
		pr_err("%s: failed to allocate cpipe_pairs\n",
				DRIVER_NAME);
		goto fail_vmalloc_cpipe_pairs;
	}
	memset(cpipe_pairs, 0, sizeof(cpipe_pairs[0]) * cpipe_npipes);
	cpipe_major = __register_chrdev(0, 0, cpipe_npipes * 2,
			DRIVER_NAME, &cpipe_fops);
	if (cpipe_major < 0) {
		err = cpipe_major;
		pr_err("%s: __register_chrdev failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_register_chrdev;
	}
	cpipe_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(cpipe_class)) {
		err = PTR_ERR(cpipe_class);
		pr_err("%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}
	for (i = 0; i < cpipe_npipes; i++) {
		err = cpipe_pair_init(&cpipe_pairs[i], i);
		if (err) {
			pr_err("%s: cpipe_pair_init failed. i = %d, "
					"err = %d\n", DRIVER_NAME, i, err);
			goto fail_cpipe_pair_init_loop;
		}
	}
	pr_info("%s: initializated successfully\n", DRIVER_NAME);
	return 0;
fail_cpipe_pair_init_loop:
	while (i--)
		cpipe_pair_destroy(&cpipe_pairs[i]);
	class_destroy(cpipe_class);
fail_class_create:
	__unregister_chrdev(cpipe_major, 0, cpipe_npipes * 2, DRIVER_NAME);
fail_register_chrdev:
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
	__unregister_chrdev(cpipe_major, 0, cpipe_npipes * 2, DRIVER_NAME);
	vfree(cpipe_pairs);
	pr_info("%s: exited successfully\n", DRIVER_NAME);
}
module_exit(cpipe_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("Pairs of char devices acting as pipes");
MODULE_VERSION("1.1.3");
