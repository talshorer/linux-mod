#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>

#include <lmod/meta.h>

#define DEEPFS_MAGIC 0x0157ae10

struct deepfs_depth_file;
struct deepfs_symlink;

struct deepfs_mount_opts {
	u8 max_depth;
};
#define DEEPFS_MAX_MAX_DEPTH \
	(typeof(((struct deepfs_mount_opts *)0)->max_depth))(-1)
#define to_deepfs_mount_opts(sb) \
	((struct deepfs_mount_opts *)(sb->s_fs_info))

struct deepfs_fs_info {
	struct deepfs_mount_opts opts;
	atomic_t next_ino;
};

struct deepfs_dir {
	struct inode *inode;
	struct dentry *dentry;
	struct deepfs_dir *parent;
	struct deepfs_depth_file *depth_file;
	struct deepfs_symlink *symlink;
	unsigned int depth;
	unsigned int id;
	struct list_head children;
	struct list_head parlink;
	struct mutex lock;
};
#define to_deepfs_dir(inode) \
	((struct deepfs_dir *)(inode->i_private))

static const char deepfs_subdir_basename[] = "sub0x";
#define DEEPFS_DIRNAME_LEN (sizeof(deepfs_subdir_basename) + 2)

# define DEEPFS_DEPTH_FILE_BUF_LEN 6 /* 0xXX\n\0 */
struct deepfs_depth_file {
	struct deepfs_dir *dir;
	struct inode *inode;
	char buf[DEEPFS_DEPTH_FILE_BUF_LEN];
};
#define to_deepfs_depth_file(inode) \
	((struct deepfs_depth_file *)(inode->i_private))

static const char deepfs_depth_file_name[] = "depth";

struct deepfs_symlink {
	struct inode *inode;
	char buf[DEEPFS_DIRNAME_LEN + 3]; /* add "../" */
};
#define to_deepfs_symlink(inode) \
	((struct deepfs_symlink *)(inode->i_private))

static const char deepfs_symlink_name[] = "link";

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
			pr_err(
			"unrecognized mount option \"%s\" or missing value\n",
					p);
			return -EINVAL;
		}
	}

	return 0;
}

static void deepfs_trace(const char *func, struct dentry *dentry)
{
	char buf[256];

	pr_info("<%s> %s\n", func, dentry_path_raw(dentry, buf, sizeof(buf)));
}

static int deepfs_depth_file_open(struct inode *inode, struct file *filp)
{
	deepfs_trace(__func__, filp->f_path.dentry);
	filp->private_data = to_deepfs_depth_file(inode);
	return 0;
}

static int deepfs_depth_file_release(struct inode *inode, struct file *filp)
{
	deepfs_trace(__func__, filp->f_path.dentry);
	filp->private_data = NULL;
	return 0;
}

static ssize_t deepfs_depth_file_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct deepfs_depth_file *f = filp->private_data;

	deepfs_trace(__func__, filp->f_path.dentry);
	return simple_read_from_buffer(buf, count, ppos, f->buf,
			DEEPFS_DEPTH_FILE_BUF_LEN);
}

static const struct file_operations deepfs_depth_file_fops = {
	.owner = THIS_MODULE,
	.open = deepfs_depth_file_open,
	.release = deepfs_depth_file_release,
	.llseek = default_llseek,
	.read = deepfs_depth_file_read,
};

static int deepfs_depth_file_create(struct super_block *sb,
		struct deepfs_dir *dir)
{
	struct inode *inode;
	struct dentry *dentry;
	struct deepfs_depth_file *f;
	struct qstr qname;
	int err;

	inode = new_inode(sb);
	if (!inode) {
		pr_err("failed to create inode\n");
		err = -ENOMEM;
		goto fail_new_inode;
	}
	inode->i_mode = S_IRUGO | S_IFREG;
	inode->i_uid.val = 0;
	inode->i_gid.val = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_fop = &deepfs_depth_file_fops;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f) {
		err = -ENOMEM;
		goto fail_kzalloc_private;
	}
	f->dir = dir;
	f->inode = inode;
	dir->depth_file = f;
	sprintf(f->buf, "0x%02x\n", dir->depth);
	inode->i_private = f;

	qname.name = deepfs_depth_file_name;
	qname.len = strlen(qname.name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(dir->dentry, &qname);
	if (!dentry) {
		pr_err("failed to create dentry\n");
		err = -ENOMEM;
		goto fail_d_alloc;
	}
	d_add(dentry, inode);

	return 0;

fail_d_alloc:
	kfree(f);
	dir->depth_file = NULL;
	inode->i_private = NULL;
fail_kzalloc_private:
	iput(inode);
fail_new_inode:
	return err;
}

static inline void deepfs_depth_file_destroy(struct deepfs_depth_file *f)
{
	kfree(f);
}


static void deepfs_free_link(void *cookie)
{
	deepfs_trace(__func__, cookie);
}

static const char *deepfs_get_link(struct dentry *dentry, struct inode *inode,
		struct delayed_call *done)
{
	struct deepfs_symlink *l = to_deepfs_symlink(dentry->d_inode);

	deepfs_trace(__func__, dentry);
	/* pass dentry as the cookie for tracing */
	set_delayed_call(done, deepfs_free_link, dentry);
	return l->buf;
}

static const struct inode_operations deepfs_symlink_inode_operations = {
	.readlink    = generic_readlink,
	.get_link = deepfs_get_link,
};

static int deepfs_symlink_create(struct super_block *sb,
		struct deepfs_dir *dir)
{
	struct inode *inode;
	struct dentry *dentry;
	struct deepfs_symlink *l;
	struct qstr qname;
	int err;

	inode = new_inode(sb);
	if (!inode) {
		pr_err("failed to create inode\n");
		err = -ENOMEM;
		goto fail_new_inode;
	}
	inode->i_mode = S_IRUGO | S_IFLNK;
	inode->i_uid.val = 0;
	inode->i_gid.val = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &deepfs_symlink_inode_operations;

	l = kzalloc(sizeof(*l), GFP_KERNEL);
	if (!l) {
		err = -ENOMEM;
		goto fail_kzalloc_private;
	}
	sprintf(l->buf, "../%s%02x", deepfs_subdir_basename, (dir->id + 1) %
			(to_deepfs_mount_opts(sb)->max_depth - dir->depth + 1)
	);
	l->inode = inode;
	dir->symlink = l;
	inode->i_private = l;

	qname.name = deepfs_symlink_name;
	qname.len = strlen(qname.name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(dir->dentry, &qname);
	if (!dentry) {
		pr_err("failed to create dentry\n");
		err = -ENOMEM;
		goto fail_d_alloc;
	}
	d_add(dentry, inode);

	return 0;

fail_d_alloc:
	kfree(l);
	inode->i_private = NULL;
	dir->symlink = NULL;
fail_kzalloc_private:
	iput(inode);
fail_new_inode:
	return err;
}

static int deepfs_fill_dirname(char *buf, struct deepfs_dir *d)
{
	return sprintf(buf, "%s%02x", deepfs_subdir_basename, d->id);
}

static int deepfs_dir_open(struct inode *inode, struct file *filp)
{
	deepfs_trace(__func__, filp->f_path.dentry);
	filp->private_data = to_deepfs_dir(inode);
	return 0;
}

static int deepfs_dir_release(struct inode *inode, struct file *filp)
{
	deepfs_trace(__func__, filp->f_path.dentry);
	filp->private_data = NULL;
	return 0;
}

static int deepfs_dir_iterate(struct file *filp, struct dir_context *ctx)
{
	struct deepfs_dir *d = filp->private_data;
	struct deepfs_dir *subdir;
	char name[DEEPFS_DIRNAME_LEN];
	unsigned int pos;

	deepfs_trace(__func__, filp->f_path.dentry);

	if (!dir_emit_dots(filp, ctx))
		return 0;
	if (ctx->pos < 4) {
		if (ctx->pos < 3) {
			if (!dir_emit(ctx, deepfs_depth_file_name,
					strlen(deepfs_depth_file_name),
					d->depth_file->inode->i_ino, DT_REG))
				return 0;
			ctx->pos++;
		}
		if (d->symlink && !dir_emit(ctx, deepfs_symlink_name,
				strlen(deepfs_symlink_name),
				d->symlink->inode->i_ino, DT_LNK))
			return 0;
		ctx->pos++;
	}
	pos = ctx->pos - 4;
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

static inline void deepfs_symlink_destroy(struct deepfs_symlink *l)
{
	kfree(l);
}

static const struct file_operations deepfs_dir_fops = {
	.owner = THIS_MODULE,
	.open = deepfs_dir_open,
	.release = deepfs_dir_release,
	.llseek = default_llseek,
	.read = generic_read_dir,
	.iterate = deepfs_dir_iterate,
};

static struct dentry *deepfs_dir_lookup(struct inode *dir,
		 struct dentry *dentry, unsigned int flags)
{
	deepfs_trace(__func__, dentry);
	return simple_lookup(dir, dentry, flags);
}

static const struct inode_operations deepfs_dir_inode_operations = {
	.lookup = deepfs_dir_lookup,
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
	inode->i_op = &deepfs_dir_inode_operations;
	inode->i_fop = &deepfs_dir_fops;

	priv = kzalloc(sizeof(struct deepfs_dir), GFP_KERNEL);
	if (!priv) {
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

		err = deepfs_symlink_create(sb, priv);
		if (err)
			pr_err("deepfs_symlink_create failed, err = %d\n",
					err);
	} else { /* mount point */
		BUG_ON(sb->s_root);
		priv->dentry = d_make_root(priv->inode);
		if (!priv->dentry) {
			pr_err("failed to create root dentry\n");
			err = -ENOMEM;
			goto fail_dentry;
		}
		sb->s_root = priv->dentry;
	}

	err = deepfs_depth_file_create(sb, priv);
	if (err)
		pr_err("deepfs_depth_file_create failed, err = %d\n", err);

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
	inode->i_private = NULL;
fail_kzalloc_private:
	iput(inode);
fail_new_inode:
	return ERR_PTR(err);
}

static inline void deepfs_dir_destroy(struct deepfs_dir *d)
{
	kfree(d);
}

static struct inode *deepfs_alloc_inode(struct super_block *sb)
{
	struct deepfs_fs_info *fsi = sb->s_fs_info;
	struct inode *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (ret) {
		inode_init_once(ret);
		ret->i_ino = atomic_inc_return(&fsi->next_ino);
	}
	return ret;
}

static void deepfs_destroy_inode(struct inode *inode)
{
	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		deepfs_dir_destroy(to_deepfs_dir(inode));
		break;
	case S_IFREG:
		deepfs_depth_file_destroy(to_deepfs_depth_file(inode));
		break;
	case S_IFLNK:
		deepfs_symlink_destroy(to_deepfs_symlink(inode));
		break;
	default:
		pr_err("no function to free inode type. i_mode = %o\n",
				inode->i_mode);
	}
	kfree(inode);
}

static struct super_operations deepfs_super_ops = {
	.statfs = simple_statfs,
	.alloc_inode = deepfs_alloc_inode,
	.destroy_inode = deepfs_destroy_inode,
};

static int deepfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct deepfs_fs_info *fsi;
	struct deepfs_dir *root;
	int err;

	save_mount_options(sb, data);

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = DEEPFS_MAGIC;
	sb->s_flags |= MS_RDONLY;
	sb->s_op = &deepfs_super_ops;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi) {
		err = -ENOMEM;
		goto fail_kzalloc_fsi;
	}
	atomic_set(&fsi->next_ino, 0);
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
	deepfs_trace(__func__, sb->s_root);
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


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("Recursive pseudo file system");
MODULE_ALIAS_FS("deepfs");
MODULE_VERSION("1.2.0");
