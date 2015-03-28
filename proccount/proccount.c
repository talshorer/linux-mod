#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <lmod/meta.h>

#define PROCCOUNT_MODE 0444

static const char DRIVER_NAME[] = "proccount";

static const char proccount_file_name[] = "opencounter";

static atomic_t proccount_counter;

static int proccount_show(struct seq_file *m, void *v)
{
	long count = (long)(m->private);

	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	seq_printf(m, "%ld\n", count);
	return 0;
}

static int proccount_open(struct inode *inode, struct file *filp)
{
	atomic_t *a = PDE_DATA(inode);
	int ret;
	long count;

	count = atomic_read(a);
	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	pr_info("%s: count = %ld\n", DRIVER_NAME, count);
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

	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	atomic_set(&proccount_counter, 0);
	pde = proc_create_data(proccount_file_name, PROCCOUNT_MODE, NULL,
			&proccount_fops, &proccount_counter);
	if (!pde) {
		pr_err("%s: proc_create_data failed\n", DRIVER_NAME);
		return -ENOMEM;
	}
	pr_info("%s: initialized successfully\n", DRIVER_NAME);
	return 0;
}
module_init(proccount_init);

static void __exit proccount_exit(void)
{
	pr_info("%s: in %s\n", DRIVER_NAME, __func__);
	remove_proc_entry(proccount_file_name, NULL);
	pr_info("%s: exited successfully\n", DRIVER_NAME);
}
module_exit(proccount_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("A file in procfs that returns open count upon read");
MODULE_VERSION("1.0.3");
