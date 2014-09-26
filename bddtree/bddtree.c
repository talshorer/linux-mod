#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>

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
	struct device device;
};
#define bddtree_device_from_device(dev) \
	container_of((dev), struct bddtree_device, device)

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
		printk(KERN_ERR "%s: <%s> kmalloc failed\n", MODULE_NAME, __func__);
	return name;
}

/* TODO device layer */

/* TODO driver sysfs control */

static int bddtree_drv_probe(struct device *dev)
{
	dev_info(dev, "<%s>\n", __func__);
	return 0;
}

static int bddtree_drv_remove(struct device *dev)
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
		printk(KERN_ERR "%s: <%s> name too long. max length is %d\n",
				MODULE_NAME, __func__, BDDTREE_DRIVER_NAME_LEN);
		goto fail_name_len_check;
	}

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: <%s> failed to allocate driver\n",
				MODULE_NAME, __func__);
		goto fail_kzalloc_drv;
	}
	/* already checked length */
	strcpy(drv->name, name);
	INIT_LIST_HEAD(&drv->link);
	drv->driver.bus = &bus->bus_type;
	drv->driver.name = drv->name;
	drv->driver.probe = bddtree_drv_probe;
	drv->driver.remove = bddtree_drv_remove;

	err = driver_register(&drv->driver);
	if (err) {
		printk(KERN_ERR "%s: <%s> driver_register, err = %d\n",
				MODULE_NAME, __func__, err);
		goto fail_driver_register;
	}

	spin_lock_irqsave(&bus->drivers_lock, flags);
	list_add(&drv->link, &bus->drivers);
	spin_unlock_irqrestore(&bus->drivers_lock, flags);

	printk(KERN_INFO "%s: created driver %s\n", MODULE_NAME, drv->name);
	return drv;

fail_driver_register:
	kfree(drv);
fail_kzalloc_drv:
fail_name_len_check:
	return ERR_PTR(err);
}

static void bddtree_driver_destroy(struct bddtree_driver *drv)
{
	unsigned long flags;
	struct bddtree_bus *bus = bddtree_bus_from_bus_type(drv->driver.bus);

	printk(KERN_INFO "%s: destroying driver %s\n", MODULE_NAME, drv->name);

	spin_lock_irqsave(&bus->drivers_lock, flags);
	list_del(&drv->link);
	spin_unlock_irqrestore(&bus->drivers_lock, flags);

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
	int ret;

	name = bddtree_buf_to_name(buf, count);
	if (!name)
		return -ENOMEM;
	drv = driver_find(name, bus);
	if (!drv) {
		ret = -EINVAL;
		printk(KERN_ERR "%s: <%s> driver %s not found\n",
				MODULE_NAME, __func__, name);
		goto out;
	}
	bddtree_driver_destroy(bddtree_driver_from_device_driver(drv));
	ret = count;

out:
	kfree(name);
	return ret;
}

static struct bus_attribute bddtree_bus_attrs[] = {
	__ATTR(add, S_IWUSR, NULL, bddtree_bus_add_store),
	__ATTR(del, S_IWUSR, NULL, bddtree_bus_del_store),
	__ATTR_NULL,
};

static int bddtree_match(struct device *dev, struct device_driver *drv)
{
	return bddtree_device_from_device(dev)->drv ==
			bddtree_driver_from_device_driver(drv);
}

static struct bddtree_bus bddtree_bus = {
	.bus_type = {
		.name = MODULE_NAME,
		.bus_attrs = bddtree_bus_attrs,
		.dev_attrs = NULL /* TODO bddtree_dev_groups */,
		.drv_attrs = NULL /* TODO bddtree_drv_groups */,
		.match = bddtree_match,
	},
};

static __init int bddtree_bus_register(struct bddtree_bus *bus)
{
	int err;

	INIT_LIST_HEAD(&bus->drivers);
	spin_lock_init(&bus->drivers_lock);
	err = bus_register(&bddtree_bus.bus_type);
	if (err) {
		printk(KERN_ERR "%s: bus_register failed. err = %d\n",
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

	printk(KERN_INFO "%s: initializated successfully\n", MODULE_NAME);
	return 0;
}
module_init(bddtree_init);

static void __exit bddtree_exit(void)
{
	bddtree_bus_unregister(&bddtree_bus);

	printk(KERN_INFO "%s: exited successfully\n", MODULE_NAME);
}
module_exit(bddtree_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A bus-driver-device tree");
MODULE_VERSION("0.1.1");
MODULE_LICENSE("GPL");
