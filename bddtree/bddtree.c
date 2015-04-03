#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <lmod/meta.h>

#define MODULE_NAME "bddtree"

struct bddtree_bus {
	struct bus_type bus_type;
	struct list_head drivers;
	spinlock_t drivers_lock;
};
#define bddtree_bus_from_bus_type(bus) \
	container_of((bus), struct bddtree_bus, bus_type)

#define BDDTREE_DRIVER_NAME_LEN 32
struct bddtree_driver {
	char name[BDDTREE_DRIVER_NAME_LEN];
	struct list_head link;
	struct device_driver driver;
	struct list_head devices;
	spinlock_t devices_lock;
};
#define bddtree_driver_from_device_driver(drv) \
	container_of((drv), struct bddtree_driver, driver)

struct bddtree_device {
	struct list_head link;
	struct bddtree_driver *drv;
	unsigned long id;
	struct device dev;
};
#define bddtree_device_from_device(_dev) \
	container_of((_dev), struct bddtree_device, dev)

static char *bddtree_buf_to_name(const char *buf, size_t count)
{
	char *name;

	if (buf[count - 1] == '\n')
		count--;
	name = kmalloc(count + 1, GFP_KERNEL);
	if (name) {
		memcpy(name, buf, count);
		name[count] = 0;
	} else
		pr_err("%s: <%s> kmalloc failed\n", MODULE_NAME,
				__func__);
	return name;
}

static void bddtree_device_release(struct device *dev)
{
	kfree(bddtree_device_from_device(dev));
}

static int bddtree_device_match(struct device *dev, void *data)
{
	return *(unsigned long *)data == bddtree_device_from_device(dev)->id;
}

static struct bddtree_device *bddtree_device_find(struct bddtree_driver *drv,
		unsigned long id)
{
	struct device *dev;

	dev = driver_find_device(&drv->driver, NULL, &id,
			bddtree_device_match);
	if (!dev)
		return NULL;
	put_device(dev); /* put away ref from driver_find_device */
	return bddtree_device_from_device(dev);
}

static struct bddtree_device *bddtree_device_create(
		struct bddtree_driver *drv, unsigned long id)
{
	struct bddtree_device *dev;
	int err;
	unsigned long flags;

	dev = bddtree_device_find(drv, id);
	if (dev) {
		err = -EINVAL;
		pr_err("%s: <%s> device with id %lu already bound "
				"to driver %s\n",
				MODULE_NAME, __func__, id, drv->name);
		goto fail_device_exists;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		pr_err("%s: <%s> failed to allocate device\n",
				MODULE_NAME, __func__);
		goto fail_kzalloc_dev;
	}
	dev->id = id;
	dev->drv = drv;
	INIT_LIST_HEAD(&dev->link);
	device_initialize(&dev->dev);
	dev->dev.bus = drv->driver.bus;
	dev_set_name(&dev->dev, "%s.%lu", drv->name, id);
	dev->dev.release = bddtree_device_release;

	err = device_add(&dev->dev);
	if (err) {
		pr_err("%s: <%s> device_add failed, err = %d\n",
				MODULE_NAME, __func__, err);
		goto fail_device_add;
	}

	spin_lock_irqsave(&drv->devices_lock, flags);
	list_add(&dev->link, &drv->devices);
	spin_unlock_irqrestore(&drv->devices_lock, flags);

	pr_info("%s: created device %s\n",
			MODULE_NAME, dev_name(&dev->dev));
	return 0;

fail_device_add:
	kfree(dev);
fail_kzalloc_dev:
fail_device_exists:
	return ERR_PTR(err);
}

static void bddtree_device_destroy(struct bddtree_device *dev)
{
	unsigned long flags;
	struct bddtree_driver *drv = dev->drv;

	pr_info("%s: destroying device %s\n",
			MODULE_NAME, dev_name(&dev->dev));

	spin_lock_irqsave(&drv->devices_lock, flags);
	list_del(&dev->link);
	spin_unlock_irqrestore(&drv->devices_lock, flags);

	device_del(&dev->dev);
	put_device(&dev->dev);
}

static ssize_t bddtree_drv_add_store(struct device_driver *drv,
		const char *buf, size_t count)
{
	struct bddtree_device *dev;
	unsigned long id;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		return ret;

	dev = bddtree_device_create(bddtree_driver_from_device_driver(drv),
			id);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	return count;
}

static ssize_t bddtree_drv_del_store(struct device_driver *_drv,
		const char *buf, size_t count)
{
	struct bddtree_driver *drv = bddtree_driver_from_device_driver(_drv);
	struct bddtree_device *dev;
	unsigned long id;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &id);
	if (ret)
		return ret;

	dev = bddtree_device_find(drv, id);
	if (!dev) {
		pr_err("%s: <%s> device %s.%lu not found\n",
				MODULE_NAME, __func__, drv->name, id);
		return -EINVAL;
	}

	bddtree_device_destroy(dev);

	return count;
}

static DRIVER_ATTR(add, S_IWUSR, NULL, bddtree_drv_add_store);
static DRIVER_ATTR(del, S_IWUSR, NULL, bddtree_drv_del_store);

static struct attribute *bddtree_drv_attrs[] = {
	&driver_attr_add.attr,
	&driver_attr_del.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bddtree_drv);

static int bddtree_probe(struct device *dev)
{
	dev_info(dev, "<%s>\n", __func__);
	return 0;
}

static int bddtree_remove(struct device *dev)
{
	dev_info(dev, "<%s>\n", __func__);
	return 0;
}

static struct bddtree_driver *bddtree_driver_create(struct bddtree_bus *bus,
		const char *name)
{
	struct bddtree_driver *drv;
	int err;
	unsigned long flags;

	if (strlen(name) >= BDDTREE_DRIVER_NAME_LEN) {
		err = -EINVAL;
		pr_err("%s: <%s> name too long. max length is %d\n",
				MODULE_NAME, __func__,
				BDDTREE_DRIVER_NAME_LEN);
		goto fail_name_len_check;
	}

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv) {
		err = -ENOMEM;
		pr_err("%s: <%s> failed to allocate driver\n",
				MODULE_NAME, __func__);
		goto fail_kzalloc_drv;
	}
	/* already checked length */
	strcpy(drv->name, name);
	INIT_LIST_HEAD(&drv->link);
	INIT_LIST_HEAD(&drv->devices);
	spin_lock_init(&drv->devices_lock);
	drv->driver.bus = &bus->bus_type;
	drv->driver.owner = THIS_MODULE;
	drv->driver.name = drv->name;
	drv->driver.probe = bddtree_probe;
	drv->driver.remove = bddtree_remove;

	err = driver_register(&drv->driver);
	if (err) {
		pr_err("%s: <%s> driver_register failed, err = %d\n",
				MODULE_NAME, __func__, err);
		goto fail_driver_register;
	}

	spin_lock_irqsave(&bus->drivers_lock, flags);
	list_add(&drv->link, &bus->drivers);
	spin_unlock_irqrestore(&bus->drivers_lock, flags);

	pr_info("%s: created driver %s\n", MODULE_NAME, drv->name);
	return drv;

fail_driver_register:
	kfree(drv);
fail_kzalloc_drv:
fail_name_len_check:
	return ERR_PTR(err);
}

static void bddtree_driver_destroy(struct bddtree_driver *drv)
{
	struct bddtree_bus *bus = bddtree_bus_from_bus_type(drv->driver.bus);
	struct bddtree_device *dev, *tmp;
	unsigned long flags;

	pr_info("%s: destroying driver %s\n", MODULE_NAME, drv->name);

	spin_lock_irqsave(&bus->drivers_lock, flags);
	list_del(&drv->link);
	spin_unlock_irqrestore(&bus->drivers_lock, flags);

	spin_lock_irqsave(&drv->devices_lock, flags);
	list_for_each_entry_safe(dev, tmp, &drv->devices, link) {
		spin_unlock_irqrestore(&drv->devices_lock, flags);
		bddtree_device_destroy(dev);
		spin_lock_irqsave(&drv->devices_lock, flags);
	}
	spin_unlock_irqrestore(&drv->devices_lock, flags);

	driver_unregister(&drv->driver);
	kfree(drv);
}

static ssize_t bddtree_bus_add_store(struct bus_type *bus,
		const char *buf, size_t count)
{
	struct bddtree_driver *drv;
	char *name;

	name = bddtree_buf_to_name(buf, count);
	if (!name)
		return -ENOMEM;
	drv = bddtree_driver_create(bddtree_bus_from_bus_type(bus), name);
	kfree(name);
	if (IS_ERR(drv))
		return PTR_ERR(drv);

	return count;
}

static ssize_t bddtree_bus_del_store(struct bus_type *bus,
		const char *buf, size_t count)
{
	struct device_driver *drv;
	char *name;
	ssize_t ret;

	name = bddtree_buf_to_name(buf, count);
	if (!name)
		return -ENOMEM;
	drv = driver_find(name, bus);
	if (!drv) {
		ret = -EINVAL;
		pr_err("%s: <%s> driver %s not found\n",
				MODULE_NAME, __func__, name);
		goto out;
	}
	bddtree_driver_destroy(bddtree_driver_from_device_driver(drv));
	ret = count;

out:
	kfree(name);
	return ret;
}

static BUS_ATTR(add, S_IWUSR, NULL, bddtree_bus_add_store);
static BUS_ATTR(del, S_IWUSR, NULL, bddtree_bus_del_store);

static struct attribute *bddtree_bus_attrs[] = {
	&bus_attr_add.attr,
	&bus_attr_del.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bddtree_bus);

static int bddtree_match(struct device *dev, struct device_driver *drv)
{
	return bddtree_device_from_device(dev)->drv ==
			bddtree_driver_from_device_driver(drv);
}

static int bddtree_uevent(struct device *_dev, struct kobj_uevent_env *env)
{
	struct bddtree_device *dev = bddtree_device_from_device(_dev);
	int err;

	err = add_uevent_var(env, "BDDTREE_DRIVER=%s", dev->drv->driver.name);
	if (err)
		return err;

	return 0;
}

static struct bddtree_bus bddtree_bus = {
	.bus_type = {
		.name = MODULE_NAME,
		.bus_groups = bddtree_bus_groups,
		.drv_groups = bddtree_drv_groups,
		.match = bddtree_match,
		.uevent = bddtree_uevent,
	},
};

static __init int bddtree_bus_register(struct bddtree_bus *bus)
{
	int err;

	INIT_LIST_HEAD(&bus->drivers);
	spin_lock_init(&bus->drivers_lock);
	err = bus_register(&bddtree_bus.bus_type);
	if (err) {
		pr_err("%s: bus_register failed. err = %d\n",
				MODULE_NAME, err);
		goto fail_bus_register;
	}

	return 0;

fail_bus_register:
	return err;
}

static void bddtree_bus_unregister(struct bddtree_bus *bus)
{
	struct bddtree_driver *drv, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&bus->drivers_lock, flags);
	list_for_each_entry_safe(drv, tmp, &bus->drivers, link) {
		spin_unlock_irqrestore(&bus->drivers_lock, flags);
		bddtree_driver_destroy(drv);
		spin_lock_irqsave(&bus->drivers_lock, flags);
	}
	spin_unlock_irqrestore(&bus->drivers_lock, flags);

	bus_unregister(&bus->bus_type);
}

static int __init bddtree_init(void)
{
	int err;

	err = bddtree_bus_register(&bddtree_bus);
	if (err)
		return err;

	pr_info("%s: initializated successfully\n", MODULE_NAME);
	return 0;
}
module_init(bddtree_init);

static void __exit bddtree_exit(void)
{
	bddtree_bus_unregister(&bddtree_bus);

	pr_info("%s: exited successfully\n", MODULE_NAME);
}
module_exit(bddtree_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("A bus-driver-device tree");
MODULE_VERSION("1.2.0");
