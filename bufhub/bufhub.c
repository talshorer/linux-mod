#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <lmod/meta.h>

#include "bufhub_ioctl.h"

#define MODULE_NAME "bufhub"

static int bufhub_max_clipboards = 16;
module_param_named(max_clipboards, bufhub_max_clipboards, int, 0444);
MODULE_PARM_DESC(
	max_clipboards,
	"maximum number of clipboards that can exist at the same time"
);

static int bufhub_clipboard_bcap = PAGE_SIZE;
module_param_named(bcap, bufhub_clipboard_bcap, int, 0444);
MODULE_PARM_DESC(bcap, "capacity for clipboard buffers");

static int __init bufhub_check_module_params(void)
{
	int err = 0;

	if (bufhub_max_clipboards <= 0) {
		pr_err("%s: bufhub_max_clipboards <= 0. value = %d\n",
				MODULE_NAME, bufhub_max_clipboards);
		err = -EINVAL;
	}
	return err;
}

/* locking policy
 * if both a slave's master_lock and the master's slaves_list_lock are
 * required for an operation, the slave's master_lock is to be locked first.
 */

struct bufhub_master {
	struct list_head slaves_list;
	spinlock_t slaves_list_lock; /* protects slaves_list */
};

struct bufhub_clipboard_dev {
	struct list_head slave_link;
	struct bufhub_master *master;
	spinlock_t master_lock; /* protects master */
	struct device *dev;
	struct kref kref;
	char *buf;
	size_t buf_len;
	struct mutex buf_mutex; /* protects buf, buf_len */
};

#define bufhub_clipboard_dev_devt(bcdev) ((bcdev)->dev->devt)

static inline int __must_check bufhub_clipboard_get(
		struct bufhub_clipboard_dev *);
static inline void bufhub_clipboard_put(struct bufhub_clipboard_dev *);

static struct class *bufhub_clipboard_class;
static int bufhub_clipboard_major;
static char bufhub_clipboard_devname[] = MODULE_NAME "_clipboard";
static struct bufhub_clipboard_dev **bufhub_clipboard_ptrs;
static DEFINE_SPINLOCK(bufhub_clipboard_ptrs_lock);

static int bufhub_clipboard_mutex_lock(struct mutex *mutex, int f_flags)
{
	if ((f_flags & O_NONBLOCK) == O_NONBLOCK) {
		if (!mutex_trylock(mutex))
			return -EAGAIN;
	} else if (mutex_lock_interruptible(mutex))
			return -ERESTARTSYS;
	return 0;
}

static ssize_t bufhub_clipboard_read(struct file *filp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct bufhub_clipboard_dev *dev = filp->private_data;
	ssize_t ret;

	ret = bufhub_clipboard_mutex_lock(&dev->buf_mutex, filp->f_flags);
	if (ret)
		return ret;
	count = min(count, (size_t)(dev->buf_len - *ppos));
	ret = copy_to_user(buf, (dev->buf + *ppos), count);
	if (ret)
		goto out;
	*ppos += count;
	ret = count;
out:
	mutex_unlock(&dev->buf_mutex);
	return ret;
}

static ssize_t bufhub_clipboard_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct bufhub_clipboard_dev *dev = filp->private_data;
	ssize_t ret;

	if (*ppos >= bufhub_clipboard_bcap)
		return -ENOSPC;
	ret = bufhub_clipboard_mutex_lock(&dev->buf_mutex, filp->f_flags);
	if (ret)
		return ret;
	count = min(count, (size_t)(bufhub_clipboard_bcap - *ppos));
	ret = copy_from_user((dev->buf + *ppos), buf, count);
	if (ret)
		goto out;
	ret = count;
	*ppos += count;
	dev->buf_len = max_t(size_t, dev->buf_len, *ppos);
out:
	mutex_unlock(&dev->buf_mutex);
	return ret;
}

static int bufhub_clipboard_open(struct inode *inode, struct file *filp)
{
	unsigned int minor = iminor(inode);
	struct bufhub_clipboard_dev *dev = bufhub_clipboard_ptrs[minor];

	if (!bufhub_clipboard_get(dev)) {
		pr_err("%s: <%s> bufhub_clipboard_get failed\n",
				MODULE_NAME, __func__);
		return -ENODEV;
	}
	filp->private_data = dev;
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		int err = bufhub_clipboard_mutex_lock(&dev->buf_mutex,
				filp->f_flags);
		if (err) {
			bufhub_clipboard_put(dev);
			return err;
		}
		dev->buf_len = 0;
		mutex_unlock(&dev->buf_mutex);
	}
	return 0;
}

static int bufhub_clipboard_release(struct inode *inode, struct file *filp)
{
	struct bufhub_clipboard_dev *dev = filp->private_data;

	filp->private_data = NULL;
	bufhub_clipboard_put(dev);
	return 0;
}

static const struct file_operations bufhub_clipboard_fops = {
	.owner = THIS_MODULE,
	.llseek = default_llseek,
	.read = bufhub_clipboard_read,
	.write = bufhub_clipboard_write,
	.open = bufhub_clipboard_open,
	.release = bufhub_clipboard_release,
};

static struct bufhub_clipboard_dev *bufhub_clipboard_create(
		struct bufhub_master *master)
{
	unsigned int i;
	int err = 0;
	unsigned long flags;
	struct bufhub_clipboard_dev *dev;
	dev_t devno;

	dev = kzalloc(sizeof(*dev) + bufhub_clipboard_bcap, GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		pr_err("%s: <%s> failed to allocate dev\n",
				MODULE_NAME, __func__);
		goto fail_kzalloc_dev;
		return ERR_PTR(-ENODEV);
	}

	spin_lock_irqsave(&bufhub_clipboard_ptrs_lock, flags);
	for (i = 0; i < bufhub_max_clipboards && bufhub_clipboard_ptrs[i]; i++)
		;
	if (i == bufhub_max_clipboards) {
		err = -ENODEV;
		pr_err("%s: <%s> all clipboards are occupied\n",
				MODULE_NAME, __func__);
		spin_unlock_irqrestore(&bufhub_clipboard_ptrs_lock, flags);
		goto fail_find_minor;
	}
	bufhub_clipboard_ptrs[i] = dev;
	spin_unlock_irqrestore(&bufhub_clipboard_ptrs_lock, flags);
	devno = MKDEV(bufhub_clipboard_major, i);

	dev->buf = (void *)(dev + 1);
	mutex_init(&dev->buf_mutex);
	spin_lock_init(&dev->master_lock);
	dev->master = master;
	INIT_LIST_HEAD(&dev->slave_link);
	kref_init(&dev->kref);
	/* block io operations until everything is in place */
	mutex_lock(&dev->buf_mutex);

	dev->dev = device_create(bufhub_clipboard_class, NULL, devno, dev,
			"%s%d", bufhub_clipboard_devname, i);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		pr_err("%s: <%s> device_create failed err=%d\n",
				MODULE_NAME, __func__, err);
		goto fail_device_create;
	}

	spin_lock_irqsave(&dev->master->slaves_list_lock, flags);
	list_add(&dev->slave_link, &dev->master->slaves_list);
	spin_unlock_irqrestore(&dev->master->slaves_list_lock, flags);

	mutex_unlock(&dev->buf_mutex);
	pr_info("%s: created clipboard %s successfully\n",
			MODULE_NAME, dev_name(dev->dev));
	return dev;

fail_device_create:
	mutex_unlock(&dev->buf_mutex);
	spin_lock_irqsave(&bufhub_clipboard_ptrs_lock, flags);
	bufhub_clipboard_ptrs[i] = NULL;
	spin_unlock_irqrestore(&bufhub_clipboard_ptrs_lock, flags);
fail_find_minor:
	kfree(dev);
fail_kzalloc_dev:
	return ERR_PTR(err);
}

static inline int __must_check bufhub_clipboard_get(
		struct bufhub_clipboard_dev *dev)
{
	return kref_get_unless_zero(&dev->kref);
}

static void bufhub_clipboard_destroy(struct bufhub_clipboard_dev *dev)
{
	unsigned long flags, flags2;

	dev_t devno = bufhub_clipboard_dev_devt(dev);

	pr_info("%s: destroying clipboard %s\n",
			MODULE_NAME, dev_name(dev->dev));

	spin_lock_irqsave(&dev->master_lock, flags);
	if (dev->master) {
		spin_lock_irqsave(&dev->master->slaves_list_lock, flags2);
		list_del(&dev->slave_link);
		spin_unlock_irqrestore(&dev->master->slaves_list_lock, flags2);
	}
	spin_unlock_irqrestore(&dev->master_lock, flags);
	device_destroy(bufhub_clipboard_class, devno);

	spin_lock_irqsave(&bufhub_clipboard_ptrs_lock, flags);
	bufhub_clipboard_ptrs[MINOR(devno)] = NULL;
	spin_unlock_irqrestore(&bufhub_clipboard_ptrs_lock, flags);

	kfree(dev);
}

static void bufhub_clipboard_kref_release(struct kref *kref)
{
	bufhub_clipboard_destroy(container_of(kref,
			struct bufhub_clipboard_dev, kref));
}

static inline void bufhub_clipboard_put(struct bufhub_clipboard_dev *dev)
{
	kref_put(&dev->kref, bufhub_clipboard_kref_release);
}

static void bufhub_clipboard_master_put(struct bufhub_clipboard_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->master->slaves_list_lock, flags);
	list_del(&dev->slave_link);
	spin_unlock_irqrestore(&dev->master->slaves_list_lock, flags);
	spin_lock_irqsave(&dev->master_lock, flags);
	dev->master = NULL;
	spin_unlock_irqrestore(&dev->master_lock, flags);
	bufhub_clipboard_put(dev);
}

static int bufhub_miscdev_open(struct inode *inode, struct file *filp)
{
	struct bufhub_master *master;

	master = kmalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;
	INIT_LIST_HEAD(&master->slaves_list);
	spin_lock_init(&master->slaves_list_lock);
	filp->private_data = master;
	return 0;
}

static int bufhub_miscdev_release(struct inode *inode, struct file *filp)
{
	struct bufhub_master *master = filp->private_data;
	struct bufhub_clipboard_dev *dev, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&master->slaves_list_lock, flags);
	list_for_each_entry_safe(dev, tmp, &master->slaves_list, slave_link) {
		spin_unlock_irqrestore(&master->slaves_list_lock, flags);
		bufhub_clipboard_master_put(dev);
		spin_lock_irqsave(&master->slaves_list_lock, flags);
	}
	spin_unlock_irqrestore(&master->slaves_list_lock, flags);
	kfree(master);
	filp->private_data = NULL;
	return 0;
}

static struct bufhub_clipboard_dev *bufhub_miscdev_ioctl_create(
		struct bufhub_master *master, unsigned int __user *uptr)
{
	int err;
	struct bufhub_clipboard_dev *dev;
	unsigned int minor;

	dev = bufhub_clipboard_create(master);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out;
	}
	minor = MINOR(bufhub_clipboard_dev_devt(dev));
	err = put_user(minor, uptr);
out:
	if (err)
		return ERR_PTR(err);
	return dev;
}

struct bufhub_clipboard_match {
	unsigned int minor;
	struct bufhub_clipboard_dev *dev;
};

static int bufhub_clipboard_match(struct device *dev, void *data)
{
	struct bufhub_clipboard_match *match = data;

	if (MINOR(dev->devt) == match->minor) {
		match->dev = dev_get_drvdata(dev);
		return -1;
	}
	return 0;
}

static int bufhub_miscdev_ioctl_destroy(
		struct bufhub_master *master, const unsigned int __user *uptr)
{
	struct bufhub_clipboard_match match;
	int err;
	unsigned long flags;

	err = get_user(match.minor, uptr);
	if (err)
		return err;
	match.dev = NULL;
	class_for_each_device(bufhub_clipboard_class, NULL, &match,
			bufhub_clipboard_match);
	if (!match.dev) {
		err = -EINVAL;
		pr_err("%s: <%s> cannot destroy nonexisting clipboard %s%d\n",
				MODULE_NAME, __func__,
				bufhub_clipboard_devname, match.minor);
		return err;
	}
	spin_lock_irqsave(&match.dev->master_lock, flags);
	if (master != match.dev->master)
		err = -EPERM;
	spin_unlock_irqrestore(&match.dev->master_lock, flags);
	if (err) {
		pr_err("%s: <%s> invalid master to destroy clipboard %s\n",
				MODULE_NAME, __func__,
				dev_name(match.dev->dev));
		return err;
	}
	bufhub_clipboard_put(match.dev);
	return 0;
}

static long bufhub_miscdev_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	struct bufhub_master *master = filp->private_data;
	long ret = 0;
	struct bufhub_clipboard_dev *dev;

	if ((_IOC_TYPE(cmd) != BUFHUB_IOC_MAGIC) ||
			(_IOC_NR(cmd) > BUFHUB_IOC_MAXNR))
		return -ENOTTY;
	switch (cmd) {
	case BUFHUB_IOCCREATE:
		dev = bufhub_miscdev_ioctl_create(master,
				(unsigned int __user *)arg);
		if (IS_ERR(dev))
			ret = PTR_ERR(dev);
		break;
	case BUFHUB_IOCDESTROY:
		ret = bufhub_miscdev_ioctl_destroy(master,
				(const unsigned int __user *)arg);
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}

static const struct file_operations bufhub_miscdev_fops = {
	.owner = THIS_MODULE,
	.open = bufhub_miscdev_open,
	.release = bufhub_miscdev_release,
	.unlocked_ioctl = bufhub_miscdev_ioctl,
};

static struct miscdevice bufhub_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &bufhub_miscdev_fops,
};

static int __init bufhub_init(void)
{
	int err = 0;

	err = bufhub_check_module_params();
	if (err)
		return err;

	bufhub_clipboard_ptrs = vmalloc(
			sizeof(bufhub_clipboard_ptrs[0]) *
					bufhub_max_clipboards);
	if (!bufhub_clipboard_ptrs) {
		pr_err("%s: failed to allocate bufhub_clipboard_ptrs\n",
				MODULE_NAME);
		goto fail_vmalloc_bufhub_clipboard_ptrs;
	}
	memset(bufhub_clipboard_ptrs, 0,
			sizeof(bufhub_clipboard_ptrs[0]) *
					bufhub_max_clipboards);

	bufhub_clipboard_major = __register_chrdev(0, 0, bufhub_max_clipboards,
			MODULE_NAME, &bufhub_clipboard_fops);
	if (bufhub_clipboard_major < 0) {
		err = bufhub_clipboard_major;
		pr_err("%s: __register_chrdev failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_register_chrdev;
	}

	bufhub_clipboard_class = class_create(THIS_MODULE,
			bufhub_clipboard_devname);
	if (IS_ERR(bufhub_clipboard_class)) {
		err = PTR_ERR(bufhub_clipboard_class);
		pr_err("%s: class_create failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_class_create;
	}

	err = misc_register(&bufhub_miscdev);
	if (err) {
		pr_err("%s: misc_register failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_misc_register;
	}

	pr_info("%s: initializated successfully\n", MODULE_NAME);
	return 0;


fail_misc_register:
	class_destroy(bufhub_clipboard_class);
fail_class_create:
	__unregister_chrdev(bufhub_clipboard_major, 0, bufhub_max_clipboards,
			MODULE_NAME);
fail_register_chrdev:
	vfree(bufhub_clipboard_ptrs);
fail_vmalloc_bufhub_clipboard_ptrs:
	return err;
}
module_init(bufhub_init);

static void __exit bufhub_exit(void)
{
	misc_deregister(&bufhub_miscdev);
	class_destroy(bufhub_clipboard_class);
	__unregister_chrdev(bufhub_clipboard_major, 0, bufhub_max_clipboards,
			MODULE_NAME);
	vfree(bufhub_clipboard_ptrs);
	pr_info("%s: exited successfully\n", MODULE_NAME);
}
module_exit(bufhub_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A misc device that allows the creation of clipboards");
MODULE_VERSION("1.0.4");
