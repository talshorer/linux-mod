#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>

#include <lmod/meta.h>

#include "elfsect.h"

static __elfsect_dummy_symbol struct { } __elfsect_dummies_empty;

static int elfsect_debugfs_dummies_show(struct seq_file *m, void *v)
{
	elfsect_dummy *fp;

	for (fp = &__start_dummies; fp < &__stop_dummies; fp++)
		seq_printf(m, "%s\n", (*fp)());
	return 0;
}

static int elfsect_debugfs_dummies_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, elfsect_debugfs_dummies_show,
			inode->i_private);
}

#define elfsect_debugfs_dummies_fname "dummies"
static const struct file_operations elfsect_debugfs_stat_fops = {
	.open = elfsect_debugfs_dummies_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static struct dentry *elfsect_debugfs;

static int __init elfsect_create_debugfs(void)
{
	int err;
	struct dentry *file;

	elfsect_debugfs = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!elfsect_debugfs) {
		err = -ENOMEM;
		pr_err("debugfs_create_dir failed\n");
		goto fail_debugfs_create_dir;
	}

	file = debugfs_create_file(elfsect_debugfs_dummies_fname, 0444,
			elfsect_debugfs, NULL, &elfsect_debugfs_stat_fops);
	if (!file) {
		err = -ENOMEM;
		pr_err("debugfs_create_file for %s\n",
				elfsect_debugfs_dummies_fname);
		goto fail_debugfs_create_file;
	}

	return 0;

fail_debugfs_create_file:
	debugfs_remove_recursive(elfsect_debugfs);
fail_debugfs_create_dir:
	return err;
}

static int __init elfsect_init(void)
{
	int err;

	err = elfsect_create_debugfs();
	if (err)
		goto fail_elfsect_create_debugfs;

	pr_info("initialized successfully\n");
	return 0;

fail_elfsect_create_debugfs:
	return err;
}
module_init(elfsect_init);

static void __exit elfsect_exit(void)
{
	debugfs_remove_recursive(elfsect_debugfs);
	pr_info("exited successfully\n");
}
module_exit(elfsect_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A module with a section of dummy function pointers");
MODULE_VERSION("1.2.2");
