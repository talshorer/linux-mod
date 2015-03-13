#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/configfs.h>
#include <linux/slab.h>

#include <lmod/meta.h>

struct confmirror_item {
	struct config_item item;
	struct kobject kobj;
	atomic_t value;
};

static struct kobject *confmirror_kobj;

static inline struct confmirror_item *kobj_to_confmirror_item(
		struct kobject *kobj)
{
	return container_of(kobj, struct confmirror_item, kobj);
}

static void confmirror_kobj_release(struct kobject *kobj)
{
	config_item_put(&kobj_to_confmirror_item(kobj)->item);
}

static void confmirror_print_attr_access(struct confmirror_item *cmi,
		const char *func, unsigned value)
{
	pr_info("<%s> %s, %u\n", func, kobject_name(&cmi->kobj), value);
}

static ssize_t confmirror_attr_show(struct confmirror_item *cmi, char *buf,
		const char *func)
{
	unsigned value = atomic_read(&cmi->value);
	confmirror_print_attr_access(cmi, func, value);
	return snprintf(buf, PAGE_SIZE, "%u\n", value);
}

static ssize_t attr_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return confmirror_attr_show(kobj_to_confmirror_item(kobj), buf,
			__func__);
}

static struct kobj_attribute confmirror_kobj_attribute = __ATTR_RO(attr);

static struct attribute *confmirror_kobj_attrs[] = {
	&confmirror_kobj_attribute.attr,
	NULL,
};

static struct kobj_type confmirror_kobj_ktype = {
	.release       = confmirror_kobj_release,
	.default_attrs = confmirror_kobj_attrs,
	.sysfs_ops     = &kobj_sysfs_ops,
};

/* expected by configfs */
static inline struct confmirror_item *to_confmirror_item(
		struct config_item *item)
{
	return item ? container_of(item, struct confmirror_item, item) : NULL;
}

CONFIGFS_ATTR_STRUCT(confmirror_item);
CONFIGFS_ATTR_OPS(confmirror_item);

#define CONFMIRROR_ATTR(_name, _mode, _show, _store) \
	struct confmirror_item_attribute confmirror_attr_##_name = \
			__CONFIGFS_ATTR(_name, _mode, _show, _store)

static ssize_t confmirror_configfs_attr_show(struct confmirror_item *cmi,
		char *page)
{
	return confmirror_attr_show(cmi, page, __func__);
}

static ssize_t confmirror_configfs_attr_store(struct confmirror_item *cmi,
		const char *page, size_t count)
{
	unsigned long value;
	if (sscanf(page, "%lu", &value) != 1)
		return -EINVAL;
	atomic_set(&cmi->value, value);
	confmirror_print_attr_access(cmi, __func__, value);
	return count;
}

static CONFMIRROR_ATTR(attr, S_IRUGO | S_IWUSR, confmirror_configfs_attr_show,
		confmirror_configfs_attr_store);

static struct configfs_attribute *confmirror_configfs_attrs[] = {
	&confmirror_attr_attr.attr,
	NULL,
};

static void confmirror_item_release(struct config_item *item)
{
	kfree(to_confmirror_item(item));
}

static struct configfs_item_operations confmirror_item_ops = {
	.release         = confmirror_item_release,
	.show_attribute  = confmirror_item_attr_show,
	.store_attribute = confmirror_item_attr_store,
};

static struct config_item_type confmirror_item_type = {
	.ct_item_ops = &confmirror_item_ops,
	.ct_attrs    = confmirror_configfs_attrs,
	.ct_owner    = THIS_MODULE,
};

static struct config_item *confmirror_make_item(struct config_group *group,
		const char *name)
{
	struct confmirror_item *cmi;
	int err;

	pr_info("<%s> %s\n", __func__, name);

	cmi = kzalloc(sizeof(struct confmirror_item), GFP_KERNEL);
	if (!cmi) {
		err = -ENOMEM;
		pr_err("<%s> failed to allocate item\n", __func__);
		goto fail_kzalloc_cmi;
	}
	config_item_init_type_name(&cmi->item, name, &confmirror_item_type);

	kobject_init(&cmi->kobj, &confmirror_kobj_ktype);
	err = kobject_add(&cmi->kobj, confmirror_kobj, "%s", name);
	if (err) {
		pr_err("<%s> kobject_add failed. err = %d\n", __func__, err);
		goto fail_kobject_add;
	}
	config_item_get(&cmi->item); /* dropped by the kobj's release */

	atomic_set(&cmi->value, 0);

	return &cmi->item;

fail_kobject_add:
	kfree(cmi);
fail_kzalloc_cmi:
	return ERR_PTR(err);
}

static void confmirror_drop_item(struct config_group *group,
		struct config_item *item)
{
	struct confmirror_item *cmi = to_confmirror_item(item);

	pr_info("<%s> %s\n", __func__, config_item_name(item));

	config_item_put(item);
	kobject_put(&cmi->kobj);
}

static struct configfs_group_operations confmirror_subsys_ops = {
	.make_item = &confmirror_make_item,
	.drop_item = &confmirror_drop_item,
};

static struct config_item_type confmirror_subsys_type = {
	.ct_group_ops = &confmirror_subsys_ops,
	.ct_owner     = THIS_MODULE,
};

static struct configfs_subsystem confmirror_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = KBUILD_MODNAME,
			.ci_type = &confmirror_subsys_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(confmirror_subsys.su_mutex),
};

static int __init confmirror_init(void)
{
	int err;

	confmirror_kobj = kobject_create_and_add(KBUILD_MODNAME, kernel_kobj);
	if (!confmirror_kobj) {
		pr_err("failed to create base kobject\n");
		err = -ENOMEM;
		goto fail_kobject_create_and_add;
	}

	config_group_init(&confmirror_subsys.su_group);
	err = configfs_register_subsystem(&confmirror_subsys);
	if (err) {
		pr_err("configfs_register_subsystem failed. err = %d\n", err);
		goto fail_configfs_register_subsystem;
	}

	pr_info("initializated successfully\n");
	return 0;

fail_configfs_register_subsystem:
	kobject_put(confmirror_kobj);
fail_kobject_create_and_add:
	return err;
}
module_init(confmirror_init);

static void __exit confmirror_exit(void)
{
	kobject_put(confmirror_kobj);
	configfs_unregister_subsystem(&confmirror_subsys);
	pr_info("exited successfully\n");
}
module_exit(confmirror_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("configfs subsystem that mirrors items to sysfs kobjects");
MODULE_VERSION("1.0.0");
