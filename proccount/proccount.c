#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <lmod/meta.h>

#define PROCCOUNT_MODE 0444

static const char proccount_file_name[] = "opencounter";

static atomic_t proccount_counter;

static int proccount_show(struct seq_file *m, void *v)
{
	long count = (long)(m->private);

	pr_info("in %s\n", __func__);
	seq_printf(m, "%ld\n", count);
	return 0;
}

static int proccount_open(struct inode *inode, struct file *filp)
{
	atomic_t *a = PDE_DATA(inode);
	int ret;
	long count;

	count = atomic_read(a);
	pr_info("in %s\n", __func__);
	pr_info("count = %ld\n", count);
	ret = single_open(filp, proccount_show, (void *)count);
	if (!ret)
		atomic_inc(a);
	return ret;
}


static const struct file_operations proccount_fops = {
	.owner = THIS_MODULE,
	.llseek = seq_lseek,
	.read = seq_read,
	.open = proccount_open,
	.release = single_release,
};

static int __init proccount_init(void)
{
	struct proc_dir_entry *pde;

	pr_info("in %s\n", __func__);
	atomic_set(&proccount_counter, 0);
	pde = proc_create_data(proccount_file_name, PROCCOUNT_MODE, NULL,
			&proccount_fops, &proccount_counter);
	if (!pde) {
		pr_err("proc_create_data failed\n");
		return -ENOMEM;
	}
	pr_info("initialized successfully\n");
	return 0;
}
module_init(proccount_init);

static void __exit proccount_exit(void)
{
	pr_info("in %s\n", __func__);
	remove_proc_entry(proccount_file_name, NULL);
	pr_info("exited successfully\n");
}
module_exit(proccount_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A file in procfs that returns open count upon read");
MODULE_VERSION("1.0.4");
