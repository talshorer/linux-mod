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
#define to_deepfs_mount_opts(sb) \
	(struct deepfs_mount_opts *)(sb->s_fs_info)

struct deepfs_fs_info {
	struct deepfs_mount_opts opts;
};

struct deepfs_dir {
	struct inode *inode;
	struct dentry *dentry;
	struct deepfs_dir *parent;
	unsigned int depth;
	unsigned int id;
	struct list_head children;
	struct list_head parlink;
	struct mutex lock;
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

static inline void deepfs_dir_destroy(struct deepfs_dir *d)
{
	kfree(d);
}

static const char deepfs_subdir_basename[] = "sub0x";
#define DEEPFS_DIRNAME_LEN (sizeof(deepfs_subdir_basename) + 2)

static int deepfs_fill_dirname(char *buf, struct deepfs_dir *d)
{
	return sprintf(buf, "%s%02x", deepfs_subdir_basename, d->id);
}

static int deepfs_dir_open(struct inode *inode, struct file *filp)
{
	pr_info("<%s>\n", __func__);
	filp->private_data = inode->i_private;
	return 0;
}

static int deepfs_dir_release(struct inode *inode, struct file *filp)
{
	pr_info("<%s>\n", __func__);
	filp->private_data = NULL;
	return 0;
}

static int deepfs_dir_iterate(struct file *filp, struct dir_context *ctx)
{
	struct deepfs_dir *d = filp->private_data;
	struct deepfs_dir *subdir;
	char name[DEEPFS_DIRNAME_LEN];
	unsigned int pos;

	pr_info("<%s>\n", __func__);

	if (!dir_emit_dots(filp, ctx))
		return 0;
	pos = ctx->pos - 2;
	list_for_each_entry(subdir, &d->children, parlink) {
		if (subdir->id < pos)
			continue;
		deepfs_fill_dirname(name, subdir);
		if (!dir_emit(ctx, name, strlen(name), subdir->inode->i_ino,
				DT_DIR))
			return 0;
		ctx->pos++;
	}

	return 0;
}

static const struct file_operations deepfs_dir_fops = {
	.owner = THIS_MODULE,
	.open = deepfs_dir_open,
	.release = deepfs_dir_release,
	.llseek = default_llseek,
	.read = generic_read_dir,
	.iterate = deepfs_dir_iterate,
};

static struct deepfs_dir *deepfs_dir_create(struct super_block *sb,
		struct deepfs_dir *parent, unsigned int depth, unsigned int id)
{
	struct deepfs_mount_opts *opts = to_deepfs_mount_opts(sb);
	struct inode *inode;
	struct deepfs_dir *priv;
	int err;

	inode = new_inode(sb);
	if (!inode) {
		pr_err("failed to create inode\n");
		err = -ENOMEM;
		goto fail_new_inode;
	}
	inode->i_mode = 0555 | S_IFDIR;
	inode->i_uid.val = 0;
	inode->i_gid.val = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &deepfs_dir_fops;

	priv = kzalloc(sizeof(struct deepfs_dir), GFP_KERNEL);
	if (!priv) {
		pr_err("failed to allocate directory private data\n");
		err = -ENOMEM;
		goto fail_kzalloc_private;
	}
	priv->depth = depth;
	priv->id = id;
	priv->inode = inode;
	INIT_LIST_HEAD(&priv->children);
	mutex_init(&priv->lock);
	inode->i_private = priv;

	if (parent) { /* not mount point */
		char name[DEEPFS_DIRNAME_LEN];
		struct qstr qname;
		deepfs_fill_dirname(name, priv);
		qname.name = name;
		qname.len = strlen(name);
		qname.hash = full_name_hash(name, qname.len);
		priv->dentry = d_alloc(parent->dentry, &qname);
		if (!priv->dentry) {
			pr_err("failed to create dentry\n");
			err = -ENOMEM;
			goto fail_dentry;
		}
		d_add(priv->dentry, inode);
	} else { /* mount point */
		priv->dentry = d_make_root(priv->inode);
		if (!priv->dentry) {
			pr_err("failed to create root dentry\n");
			err = -ENOMEM;
			goto fail_dentry;
		}
		sb->s_root = priv->dentry;
	}

	if (depth < opts->max_depth) {
		unsigned int i;
		struct deepfs_dir *subdir;
		unsigned int children_depth = depth + 1;
		for (i = 0; i < opts->max_depth - priv->depth; i++) {
			subdir = deepfs_dir_create(sb, priv,
					children_depth, i);
			if (IS_ERR(subdir)) {
				pr_err("deepfs_dir_create failed, err = %d\n",
						(int)PTR_ERR(subdir));
			} else {
				list_add_tail(&subdir->parlink,
						&priv->children);
			}
		}
	}

	return priv;

fail_dentry:
	kfree(priv);
fail_kzalloc_private:
	iput(inode);
fail_new_inode:
	return ERR_PTR(err);
}

static void deepfs_destroy_inode(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode))
		deepfs_dir_destroy(to_deepfs_dir(inode));
}

struct super_operations deepfs_super_ops = {
	.statfs = simple_statfs,
	.destroy_inode = deepfs_destroy_inode,
};

static int deepfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct deepfs_fs_info *fsi;
	struct deepfs_dir *root;
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

	root = deepfs_dir_create(sb, NULL, 0, 0);
	if (IS_ERR(root)) {
		pr_err("failed to create root inode\n");
		err = PTR_ERR(root);
		goto fail_deepfs_dir_create;
	}

	return 0;

fail_deepfs_dir_create:
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
MODULE_VERSION("0.2.0");
MODULE_LICENSE("GPL");
