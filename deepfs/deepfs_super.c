#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/parser.h>

#define DEEPFS_MAGIC 0x0157ae10

struct deepfs_mount_opts {
	u8 max_depth;
};
#define DEEPFS_MAX_MAX_DEPTH \
	(typeof(((struct deepfs_mount_opts *)0)->max_depth))(-1)

struct deepfs_fs_info {
	struct deepfs_mount_opts opts;
};

enum {
	deepfs_opt_max_depth,
	deepfs_opt_err,
};

static const match_table_t deepfs_tokens = {
	{deepfs_opt_max_depth, "max_depth=%u"},
	{deepfs_opt_err, NULL}
};

static int deepfs_parse_options(char *data, struct deepfs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int token;
	int option;
	char *p;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;
		token = match_token(p, deepfs_tokens, args);
		switch (token) {
		case deepfs_opt_max_depth:
			if (match_int(&args[0], &option) ||
					option >= DEEPFS_MAX_MAX_DEPTH) {
				pr_err("invalid max_depth\n");
				return -EINVAL;
			}
			opts->max_depth = option;
			break;
		default:
			pr_err("unrecognized mount option \"%s\" "
					"or missing value\n", p);
			return -EINVAL;
		}
	}

	return 0;
}

static const struct super_operations deepfs_super_operations = {
	.statfs = simple_statfs,
};

static int deepfs_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr files[] = {{""}};
	struct deepfs_fs_info *fsi;
	int err;

	save_mount_options(sb, data);

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi) {
		pr_err("failed to allocate fs info\n");
		err = -ENOMEM;
		goto fail_kzalloc_fsi;
	}
	sb->s_fs_info = fsi;

	err = deepfs_parse_options(data, &fsi->opts);
	if (err)
		goto fail_deepfs_parse_options;

	err = simple_fill_super(sb, DEEPFS_MAGIC, files);
	if (err)
		goto fail_simple_fill_super;

	sb->s_op = &deepfs_super_operations;

	return 0;

fail_simple_fill_super:
fail_deepfs_parse_options:
	kfree(fsi);
	sb->s_fs_info = NULL;
fail_kzalloc_fsi:
	return err;
}

static void deepfs_kill_sb(struct super_block *sb)
{
	pr_info("<%s>\n", __func__);
	kill_litter_super(sb);
}

static struct dentry *deepfs_mount(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data)
{
	pr_info("<%s>\n", __func__);
	return mount_single(fs_type, flags, data, deepfs_fill_super);
}

static struct file_system_type deepfs_fs_type = {
	.owner = THIS_MODULE,
	.name = KBUILD_MODNAME,
	.mount = deepfs_mount,
	.kill_sb = deepfs_kill_sb,
};

static int __init deepfs_init(void)
{
	return register_filesystem(&deepfs_fs_type);
}
module_init(deepfs_init);

static void __exit deepfs_exit(void)
{
	unregister_filesystem(&deepfs_fs_type);
}
module_exit(deepfs_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Recursive pseudo file system");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");
