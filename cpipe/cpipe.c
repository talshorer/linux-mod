#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define CPIPE_MAGIC_FIRST_MINOR 0

static const char DRIVER_NAME[] = "cpipe";

struct cpipe_buf {
	void *data;
	size_t len;
	size_t cap;
};

struct cpipe_dev {
	struct cpipe_buf rbuf;
	struct mutex rmutex; /* protects rbuf */
	wait_queue_head_t rq, wq;
	struct cdev cdev;
	struct device *dev;
	struct cpipe_dev *twin;
};

#define cpipe_dev_devt(cpdev) ((cpdev)->cdev.dev)
#define cpipe_dev_kobj(cpdev) (&(cpdev)->dev->kobj)
#define cpipe_dev_name(cpdev) (cpipe_dev_kobj(cpdev)->name)

/* REVISIT needed? */
struct cpipe_pair {
	struct cpipe_dev devices[2];
};

static int cpipe_npipes = 1;
module_param_named(npipes, cpipe_npipes, int, 0);
MODULE_PARM_DESC(npipes, "number of pipes to create");

static int cpipe_bsize = PAGE_SIZE;
module_param_named(bsize, cpipe_bsize, int, 0);
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
	return err;
}

static struct cpipe_pair *cpipe_pairs;
static dev_t cpipe_dev_base;
static struct class *cpipe_class;
static const char cpipe_twin_link_name[] = "twin";

struct file_operations cpipe_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	/* TODO add opertaions
	 * open
	 * release
	 * read
	 * write
	 * poll
	 * ioctl
	 */
};

static int __init cpipe_dev_init(struct cpipe_dev *dev, int i, int j)
{
	int err;
	dev_t devno = MKDEV(MAJOR(cpipe_dev_base), i * 2 + j);
	dev->rbuf.data = vmalloc(cpipe_bsize);
	if (!dev->rbuf.data) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate dev->rbuf.data "
				"i=%d j=%d\n", DRIVER_NAME, i, j);
		goto fail_vmalloc_dev_rbuf_data;
	}
	dev->rbuf.len = 0;
	dev->rbuf.cap = cpipe_bsize;
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
	vfree(dev->rbuf.data);
fail_vmalloc_dev_rbuf_data:
	return err;
}

static void cpipe_dev_destroy(struct cpipe_dev *dev)
{
	printk(KERN_INFO "%s: destroying device %s\n", DRIVER_NAME, cpipe_dev_name(dev));
	device_destroy(cpipe_class, cpipe_dev_devt(dev));
	cdev_del(&dev->cdev);
	vfree(dev->rbuf.data);
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
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

