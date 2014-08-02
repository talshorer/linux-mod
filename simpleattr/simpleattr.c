#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h> /* to extract pid */
#include <linux/device.h>

#define SIMPLEATTR_MAGIC_FIRST_MINOR 0
#define SIMPLEATTR_SYSFS_PERM (0666)

static char DRIVER_NAME[] = "simpleattr";

static int simpleattr_ndevices = -1;
module_param_named(ndevices, simpleattr_ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "number of simpleattr devices to create");

/* these declarations are needed for the DEVICE_ATTR declaration */
static ssize_t simpleattr_sys_attr_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t simpleattr_sys_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR(attr, SIMPLEATTR_SYSFS_PERM,
		simpleattr_sys_attr_show, simpleattr_sys_attr_store);

static struct class *simpleattr_class = NULL;
static dev_t simpleattr_dev_base;
static struct device **simpleattr_devices;

static void simpleattr_print_sys_attr_access(struct device *dev,
		struct device_attribute *attr, int val, const char *func)
{
	printk(KERN_INFO "%s: sysfs access: %s", DRIVER_NAME, func);
	printk(KERN_INFO "%s: \tdevice %s\n", DRIVER_NAME, dev->kobj.name);
	printk(KERN_INFO "%s: \tattribute %s\n", DRIVER_NAME, attr->attr.name);
	printk(KERN_INFO "%s: \tprocess %d\n", DRIVER_NAME, current->pid);
	printk(KERN_INFO "%s: \tvalue %d\n", DRIVER_NAME, val);
}

static ssize_t simpleattr_sys_attr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = 0;
	simpleattr_print_sys_attr_access(dev, attr, val, __func__);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t simpleattr_sys_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;
	simpleattr_print_sys_attr_access(dev, attr, val, __func__);
	return count;
}

static struct device __init *simpleattr_device_create(int i) {
	struct device *dev;
	int err;
	dev = device_create(simpleattr_class, NULL,
			MKDEV(MAJOR(simpleattr_dev_base), i), NULL,
			"%s%d", DRIVER_NAME, i);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		printk(KERN_ERR "%s: device_create failed. i = %d, "
			"err = %d\n", DRIVER_NAME, i, err);
		goto fail_device_create;
	}
	err = device_create_file(dev, &dev_attr_attr);
	if (err) {
		printk(KERN_ERR "%s: device_create_file failed. i = %d, "
			"err = %d\n", DRIVER_NAME, i, err);
		goto fail_device_create_file;
	}
	printk(KERN_INFO "%s: created device %s successfully\n",
			DRIVER_NAME, dev->kobj.name);
	return dev;
fail_device_create_file:
	device_destroy(simpleattr_class, i);
fail_device_create:
	return ERR_PTR(err);
}

static void simpleattr_device_destroy(int i) {
	struct device *dev = simpleattr_devices[i];
	printk(KERN_INFO "%s: destroying device %s\n",
			DRIVER_NAME, dev->kobj.name);
	device_remove_file(dev, &dev_attr_attr);
	device_destroy(simpleattr_class, MKDEV(MAJOR(simpleattr_dev_base), i));
}

static int __init simpleattr_init(void)
{
	int err;
	int i;
	struct device *dev;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	if (simpleattr_ndevices < 0) {
		printk(KERN_ERR "%s: simpleattr_ndevices < 0. value = %d\n",
				DRIVER_NAME, simpleattr_ndevices);
		return -EINVAL;
	}
	simpleattr_devices = kmalloc(sizeof(simpleattr_devices[0]) * simpleattr_ndevices,
			GFP_KERNEL);
	if (!simpleattr_devices) {
		printk(KERN_ERR "%s: failed to allocate simpleattr_devices\n",
				DRIVER_NAME);
		err = -ENOMEM;
		goto fail_kmalloc_simpleattr_devices;
	}
	err = alloc_chrdev_region(&simpleattr_dev_base, SIMPLEATTR_MAGIC_FIRST_MINOR,
			simpleattr_ndevices, DRIVER_NAME);
	if (err) {
		printk(KERN_ERR "%s: alloc_chrdev_region failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_alloc_chrdev_region;
	}
	simpleattr_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(simpleattr_class)) {
		err = PTR_ERR(simpleattr_class);
		printk(KERN_ERR "%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}
	for (i = 0; i < simpleattr_ndevices; i++) {
		dev = simpleattr_device_create(i);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			printk(KERN_ERR "%s: unwinding\n", DRIVER_NAME);
			goto fail_simpleattr_device_create_loop;
		}
		simpleattr_devices[i] = dev;
	}
	printk(KERN_INFO "%s: initialized successfully\n", DRIVER_NAME);
	return 0;
fail_simpleattr_device_create_loop:
	/* device at [i] isn't created */
	while (i--)
		simpleattr_device_destroy(i);
	class_destroy(simpleattr_class);
fail_class_create:
	unregister_chrdev_region(simpleattr_dev_base, simpleattr_ndevices);
fail_alloc_chrdev_region:
	kfree(simpleattr_devices);
fail_kmalloc_simpleattr_devices:
	return err;
}
module_init(simpleattr_init);

static void __exit simpleattr_exit(void)
{
	int i;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	for (i = 0; i < simpleattr_ndevices; i++)
		simpleattr_device_destroy(i);
	class_destroy(simpleattr_class);
	unregister_chrdev_region(simpleattr_dev_base, simpleattr_ndevices);
	kfree(simpleattr_devices);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(simpleattr_exit);

MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A simple dummy device with a sysfs attribute");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");

