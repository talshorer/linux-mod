#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "bufhub_ioctl.h"

#define MODULE_NAME "bufhub"

#define BUFHUB_MAGIC_FIRST_MINOR 0

static int bufhub_max_clipboards = 16;
module_param_named(max_clipboards, bufhub_max_clipboards, int, 0444);
MODULE_PARM_DESC(max_clipboards,
		"maximum number of clipboards that can exist at the same time");

static int bufhub_clipboard_bcap = PAGE_SIZE;
module_param_named(bcap, bufhub_clipboard_bcap, int, 0444);
MODULE_PARM_DESC(bcap, "capacity for clipboard buffers");

static int __init bufhub_check_module_params(void) {
	int err = 0;
	if (bufhub_max_clipboards <= 0) {
		printk(KERN_ERR "%s: bufhub_max_clipboards <= 0. value = %d\n",
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
	struct cdev cdev;
	char *buf;
	size_t len;
	struct mutex buf_mutex; /* protects buf, len */
};
#define bufhub_clipboard_dev_devt(bcdev) ((bcdev)->dev->devt)

static struct class *bufhub_clipboard_class;
static dev_t bufhub_clipboard_dev_base;
static char bufhub_clipboard_devname[] = (MODULE_NAME "_clipboard");
static bool *bufhub_clipboard_occupied;
static DEFINE_SPINLOCK(bufhub_clipboard_occupied_lock);

static struct file_operations bufhub_clipboard_fops = {
	.owner = THIS_MODULE,
	/* TODO open, release, read, write, poll */
};

static struct bufhub_clipboard_dev *bufhub_clipboard_create(
		struct bufhub_master *master)
{
	unsigned int i;
	int err = 0;
	unsigned long flags;
	struct bufhub_clipboard_dev *dev;
	dev_t devno;

	spin_lock_irqsave(&bufhub_clipboard_occupied_lock, flags);
	for (i = 0; i < bufhub_max_clipboards && bufhub_clipboard_occupied[i]; i++)
		;
	if (i == bufhub_max_clipboards) {
		err = -ENODEV;
		printk(KERN_ERR "%s: <%s> all clipboards are occupied\n",
				MODULE_NAME, __func__);
		spin_unlock_irqrestore(&bufhub_clipboard_occupied_lock, flags);
		goto fail_find_minor;
	}
	bufhub_clipboard_occupied[i] = true;
	spin_unlock_irqrestore(&bufhub_clipboard_occupied_lock, flags);
	devno = MKDEV(MAJOR(bufhub_clipboard_dev_base), i);

	dev = kzalloc(sizeof(*dev) + bufhub_clipboard_bcap, GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: <%s> failed to allocate dev\n",
				MODULE_NAME, __func__);
		goto fail_kzalloc_dev;
		return ERR_PTR(-ENODEV);
	}

	dev->buf = (void *)(dev + 1);
	mutex_init(&dev->buf_mutex);
	spin_lock_init(&dev->master_lock);
	dev->master = master;
	INIT_LIST_HEAD(&dev->slave_link);
	kref_init(&dev->kref);
	/* block io operations until everything is in place */
	mutex_lock(&dev->buf_mutex);

	dev->dev = device_create(bufhub_clipboard_class, NULL, devno, dev, "%s%d",
			bufhub_clipboard_devname, i);
	if (IS_ERR(dev->dev)) {
		err = PTR_ERR(dev->dev);
		printk(KERN_ERR "%s: <%s> device_create failed err=%d\n",
				MODULE_NAME, __func__, err);
		goto fail_device_create;
	}

	cdev_init(&dev->cdev, &bufhub_clipboard_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err) {
		printk(KERN_ERR "%s: <%s> cdev_add failed err=%d\n",
				MODULE_NAME, __func__, err);
		goto fail_cdev_add;
	}

	spin_lock_irqsave(&dev->master->slaves_list_lock, flags);
	list_add(&dev->slave_link, &dev->master->slaves_list);
	spin_unlock_irqrestore(&dev->master->slaves_list_lock, flags);

	mutex_unlock(&dev->buf_mutex);
	printk(KERN_INFO "%s: created clipboard %s successfully\n",
			MODULE_NAME, dev_name(dev->dev));
	return dev;

fail_cdev_add:
	device_destroy(bufhub_clipboard_class, devno);
fail_device_create:
	mutex_unlock(&dev->buf_mutex);
	kfree(dev);
fail_kzalloc_dev:
	spin_lock_irqsave(&bufhub_clipboard_occupied_lock, flags);
	bufhub_clipboard_occupied[i] = false;
	spin_unlock_irqrestore(&bufhub_clipboard_occupied_lock, flags);
fail_find_minor:
	return ERR_PTR(err);
}

static inline void bufhub_clipboard_get(struct bufhub_clipboard_dev *dev)
{
	kref_get(&dev->kref);
}

static void bufhub_clipboard_destroy(struct bufhub_clipboard_dev *dev)
{
	unsigned long flags, flags2;

	dev_t devno = bufhub_clipboard_dev_devt(dev);
	printk(KERN_INFO "%s: destroying clipboard %s\n",
			MODULE_NAME, dev_name(dev->dev));

	spin_lock_irqsave(&dev->master_lock, flags);
	if (dev->master) {
		spin_lock_irqsave(&dev->master->slaves_list_lock, flags2);
		list_del(&dev->slave_link);
		spin_unlock_irqrestore(&dev->master->slaves_list_lock, flags2);
	}
	spin_unlock_irqrestore(&dev->master_lock, flags);

	cdev_del(&dev->cdev);
	device_destroy(bufhub_clipboard_class, devno);
	kfree(dev);

	spin_lock_irqsave(&bufhub_clipboard_occupied_lock, flags);
	bufhub_clipboard_occupied[MINOR(devno)] = false;
	spin_unlock_irqrestore(&bufhub_clipboard_occupied_lock, flags);
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
	if (!master) {
		printk(KERN_ERR "%s: <%s> failed to allocate master\n",
				MODULE_NAME, __func__);
		return -ENOMEM;
	}
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
		printk(KERN_ERR "%s: <%s> cannot destroy nonexisting clipboard "
				"%s%d\n", MODULE_NAME, __func__, bufhub_clipboard_devname,
				match.minor);
		return err;
	}
	spin_lock_irqsave(&match.dev->master_lock, flags);
	if (master != match.dev->master)
		err = -EPERM;
	spin_unlock_irqrestore(&match.dev->master_lock, flags);
	if (err) {
		printk(KERN_ERR "%s: <%s> invalid master to destroy clipboard %s\n",
				MODULE_NAME, __func__, dev_name(match.dev->dev));
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
		dev = bufhub_miscdev_ioctl_create(master, (unsigned int __user *)arg);
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

static struct file_operations bufhub_miscdev_fops = {
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

	bufhub_clipboard_occupied = vmalloc(
			sizeof(bufhub_clipboard_occupied[0]) * bufhub_max_clipboards);
	if (!bufhub_clipboard_occupied) {
		printk(KERN_ERR "%s: failed to allocate bufhub_clipboard_occupied\n",
				MODULE_NAME);
		goto fail_vmalloc_bufhub_clipboard_occupied;
	}
	memset(bufhub_clipboard_occupied, 0,
			sizeof(bufhub_clipboard_occupied[0]) * bufhub_max_clipboards);

	err = alloc_chrdev_region(&bufhub_clipboard_dev_base,
			BUFHUB_MAGIC_FIRST_MINOR, bufhub_max_clipboards, MODULE_NAME);
	if (err) {
		printk(KERN_ERR "%s: alloc_chrdev_region failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_alloc_chrdev_region;
	}

	bufhub_clipboard_class = class_create(THIS_MODULE,
			bufhub_clipboard_devname);
	if (IS_ERR(bufhub_clipboard_class)) {
		err = PTR_ERR(bufhub_clipboard_class);
		printk(KERN_ERR "%s: class_create failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_class_create;
	}

	err = misc_register(&bufhub_miscdev);
	if (err) {
		printk(KERN_ERR "%s: misc_register failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_misc_register;
	}

	printk(KERN_INFO "%s: initializated successfully\n", MODULE_NAME);
	return 0;


fail_misc_register:
	class_destroy(bufhub_clipboard_class);
fail_class_create:
	unregister_chrdev_region(bufhub_clipboard_dev_base, bufhub_max_clipboards);
fail_alloc_chrdev_region:
	vfree(bufhub_clipboard_occupied);
fail_vmalloc_bufhub_clipboard_occupied:
	return err;
}
module_init(bufhub_init);

static void __exit bufhub_exit(void)
{
	misc_deregister(&bufhub_miscdev);
	class_destroy(bufhub_clipboard_class);
	unregister_chrdev_region(bufhub_clipboard_dev_base, bufhub_max_clipboards);
	vfree(bufhub_clipboard_occupied);
	printk(KERN_INFO "%s: exited successfully\n", MODULE_NAME);
}
module_exit(bufhub_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A misc device that allows the creation of clipboards");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");
