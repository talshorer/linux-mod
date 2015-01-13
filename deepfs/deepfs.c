#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/pagemap.h>

#define DEEPFS_MAGIC 0x0157ae10

struct deepfs_mount_opts {
	u8 max_depth;
};
#define DEEPFS_MAX_MAX_DEPTH \
	(typeof(((struct deepfs_mount_opts *)0)->max_depth))(-1)

struct deepfs_fs_info {
	struct deepfs_mount_opts opts;
};

struct deepfs_dir {
	struct inode *inode;
	struct kref kref;
	unsigned int depth;
	unsigned int id;
};

#define to_deepfs_dir(inode) \
	((struct deepfs_dir *)(inode->i_private))

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

static inline int __must_check deepfs_dir_get(struct deepfs_dir *d)
{
	return kref_get_unless_zero(&d->kref);
};


static void deepfs_dir_destroy(struct deepfs_dir *d)
{
	iput(d->inode);
	kfree(d);
}

static void deepfs_dir_kref_release(struct kref *kref)
{
	deepfs_dir_destroy(container_of(kref, struct deepfs_dir, kref));
}

static inline void deepfs_dir_put(struct deepfs_dir *d)
{
	kref_put(&d->kref, deepfs_dir_kref_release);
}

static int deepfs_dir_open(struct inode *inode, struct file *filp)
{
	pr_info("<%s>\n", __func__);
	return 0;
}

static int deepfs_dir_release(struct inode *inode, struct file *filp)
{
	pr_info("<%s>\n", __func__);
	return 0;
}

static int deepfs_dir_iterate(struct file *filp, struct dir_context *cts)
{
	pr_info("<%s>\n", __func__);
	return 0;
}

static const struct file_operations deepfs_dir_fops = {
	.owner = THIS_MODULE,
	.open = deepfs_dir_open,
	.release = deepfs_dir_release,
	.iterate = deepfs_dir_iterate,
};

static struct inode *deepfs_create_dir(struct super_block *sb, umode_t mode,
		unsigned int depth, unsigned int id)
{
	struct inode *ret;
	struct deepfs_dir *priv;
	int err;

	ret = new_inode(sb);
	if (!ret) {
		pr_err("failed to create inode\n");
		err = -ENOMEM;
		goto fail_new_inode;
	}
	ret->i_mode = mode | S_IFDIR;
	ret->i_uid.val = 0;
	ret->i_gid.val = 0;
	ret->i_blocks = 0;
	ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
	ret->i_op = &simple_dir_inode_operations;
	ret->i_fop = &deepfs_dir_fops;

	priv = kzalloc(sizeof(struct deepfs_dir), GFP_KERNEL);
	if (!priv) {
		pr_err("failed to allocate directory private data\n");
		err = -ENOMEM;
		goto fail_kzalloc_private;
	}
	priv->depth = depth;
	priv->id = 0;
	priv->inode = ret;
	kref_init(&priv->kref);
	ret->i_private = priv;

	return ret;

fail_kzalloc_private:
	iput(ret);
fail_new_inode:
	return ERR_PTR(err);
}

struct super_operations deepfs_super_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
};

static int deepfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct deepfs_fs_info *fsi;
	struct inode *root;
	struct dentry *root_dentry;
	int err;

	save_mount_options(sb, data);

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = DEEPFS_MAGIC;
	sb->s_flags |= MS_RDONLY;
	sb->s_op = &deepfs_super_ops;

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

	root = deepfs_create_dir(sb, 0755, 0, 0);
	if (IS_ERR(root)) {
		pr_err("failed to create root inode\n");
		err = PTR_ERR(root);
		goto fail_deepfs_create_dir;
	}

	root_dentry = d_make_root(root);
	if (!root_dentry) {
		pr_err("failed to create root dentry\n");
		err = -ENOMEM;
		goto fail_d_make_root;
	}
	sb->s_root = root_dentry;

	return 0;

fail_d_make_root:
	deepfs_dir_put(to_deepfs_dir(root));
fail_deepfs_create_dir:
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
	return mount_nodev(fs_type, flags, data, deepfs_fill_super);
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
MODULE_VERSION("0.1.3");
MODULE_LICENSE("GPL");
