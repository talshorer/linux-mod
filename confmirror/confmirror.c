#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/configfs.h>
#include <linux/slab.h>

#include <lmod/meta.h>

struct confmirror_item {
	struct config_item item;
};

static struct kobject *confmirror_kobj;

static inline struct confmirror_item *to_confmirror_item(
		struct config_item *item)
{
	return item ? container_of(item, struct confmirror_item, item) : NULL;
}

CONFIGFS_ATTR_STRUCT(confmirror_item);
CONFIGFS_ATTR_OPS(confmirror_item);

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
	/* .ct_attrs    = confmirror_item_attrs, */
	.ct_owner    = THIS_MODULE,
};

static struct config_item *confmirror_make_item(struct config_group *group,
		const char *name)
{
	struct confmirror_item *cmi;

	pr_info("<%s> %s\n", __func__, name);

	cmi = kzalloc(sizeof(struct confmirror_item), GFP_KERNEL);
	if (!cmi)
		return ERR_PTR(-ENOMEM);
	config_item_init_type_name(&cmi->item, name, &confmirror_item_type);

	return &cmi->item;
}

static void confmirror_drop_item(struct config_group *group,
		struct config_item *item)
{
	pr_info("<%s> %s\n", __func__, config_item_name(item));
	config_item_put(item);
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
	configfs_register_subsystem(&confmirror_subsys);
	pr_info("exited successfully\n");
}
module_exit(confmirror_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("configfs subsystem that mirrors items to sysfs kobjects");
MODULE_VERSION("0.0.1");
