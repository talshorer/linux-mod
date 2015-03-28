#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h> /* to extract pid */
#include <linux/device.h>

#include <lmod/meta.h>

#define SIMPLEATTR_SYSFS_PERM (0644)

static char DRIVER_NAME[] = "simpleattr";

static int simpleattr_ndevices = 1;
module_param_named(ndevices, simpleattr_ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "number of simpleattr devices to create");

/* these declarations are needed for the DEVICE_ATTR declaration */
static ssize_t simpleattr_sys_attr_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t simpleattr_sys_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static DEVICE_ATTR(attr, SIMPLEATTR_SYSFS_PERM,
		simpleattr_sys_attr_show, simpleattr_sys_attr_store);

static struct class *simpleattr_class;
static struct device **simpleattr_devices;

static void simpleattr_print_sys_attr_access(struct device *dev,
		struct device_attribute *attr, unsigned long val,
		const char *func)
{
	pr_info("%s: sysfs access: %s", DRIVER_NAME, func);
	pr_info("%s: \tdevice %s\n", DRIVER_NAME, dev->kobj.name);
	pr_info("%s: \tattribute %s\n", DRIVER_NAME, attr->attr.name);
	pr_info("%s: \tprocess %d\n", DRIVER_NAME, current->pid);
	pr_info("%s: \tvalue %lu\n", DRIVER_NAME, val);
}

static ssize_t simpleattr_sys_attr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned long val = (unsigned long)dev_get_drvdata(dev);
	simpleattr_print_sys_attr_access(dev, attr, val, __func__);
	return snprintf(buf, PAGE_SIZE, "%lu\n", val);
}

static ssize_t simpleattr_sys_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val;
	if (sscanf(buf, "%lu", &val) != 1)
		return -EINVAL;
	simpleattr_print_sys_attr_access(dev, attr, val, __func__);
	dev_set_drvdata(dev, (void *)val);
	return count;
}

#define simpleattr_MKDEV(i) MKDEV(0, i)

static struct device __init *simpleattr_device_create(int i) {
	struct device *dev;
	int err;
	dev = device_create(simpleattr_class, NULL, simpleattr_MKDEV(i), NULL,
			"%s%d", DRIVER_NAME, i);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		pr_err("%s: device_create failed. i = %d, "
			"err = %d\n", DRIVER_NAME, i, err);
		goto fail_device_create;
	}
	err = device_create_file(dev, &dev_attr_attr);
	if (err) {
		pr_err("%s: device_create_file failed. i = %d, "
			"err = %d\n", DRIVER_NAME, i, err);
		goto fail_device_create_file;
	}
	pr_info("%s: created device %s successfully\n",
			DRIVER_NAME, dev->kobj.name);
	return dev;
fail_device_create_file:
	device_destroy(simpleattr_class, simpleattr_MKDEV(i));
fail_device_create:
	return ERR_PTR(err);
}

static void simpleattr_device_destroy(int i) {
	struct device *dev = simpleattr_devices[i];
	pr_info("%s: destroying device %s\n",
			DRIVER_NAME, dev->kobj.name);
	device_remove_file(dev, &dev_attr_attr);
	device_destroy(simpleattr_class, simpleattr_MKDEV(i));
}

static int __init simpleattr_init(void)
{
	int err;
	int i;
	struct device *dev;
	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	if (simpleattr_ndevices < 0) {
		pr_err("%s: simpleattr_ndevices < 0. value = %d\n",
				DRIVER_NAME, simpleattr_ndevices);
		return -EINVAL;
	}
	simpleattr_devices = kmalloc(
			sizeof(simpleattr_devices[0]) * simpleattr_ndevices,
			GFP_KERNEL);
	if (!simpleattr_devices) {
		pr_err("%s: failed to allocate simpleattr_devices\n",
				DRIVER_NAME);
		err = -ENOMEM;
		goto fail_kmalloc_simpleattr_devices;
	}
	simpleattr_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(simpleattr_class)) {
		err = PTR_ERR(simpleattr_class);
		pr_err("%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}
	for (i = 0; i < simpleattr_ndevices; i++) {
		dev = simpleattr_device_create(i);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			pr_err("%s: unwinding\n", DRIVER_NAME);
			goto fail_simpleattr_device_create_loop;
		}
		simpleattr_devices[i] = dev;
	}
	pr_info("%s: initialized successfully\n", DRIVER_NAME);
	return 0;
fail_simpleattr_device_create_loop:
	/* device at [i] isn't created */
	while (i--)
		simpleattr_device_destroy(i);
	class_destroy(simpleattr_class);
fail_class_create:
	kfree(simpleattr_devices);
fail_kmalloc_simpleattr_devices:
	return err;
}
module_init(simpleattr_init);

static void __exit simpleattr_exit(void)
{
	int i;
	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	for (i = 0; i < simpleattr_ndevices; i++)
		simpleattr_device_destroy(i);
	class_destroy(simpleattr_class);
	kfree(simpleattr_devices);
	pr_info("%s: exited successfully\n", DRIVER_NAME);
}
module_exit(simpleattr_exit);

LMOD_MODULE_META();
MODULE_DESCRIPTION("A simple dummy device with a sysfs attribute");
MODULE_VERSION("1.2.2");
