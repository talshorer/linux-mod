#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define PROCCOUNT_MODE 0444

static const char DRIVER_NAME[] = "proccount";

static const char proccount_file_name[] = "opencounter";

static atomic_t proccount_counter;

static int proccount_show(struct seq_file *m, void *v)
{
	long count = (long)(m->private);
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	seq_printf(m, "%ld\n", count);
	return 0;
}

static int proccount_open(struct inode *inode, struct file *filp)
{
	atomic_t *a = PDE_DATA(inode);
	int ret;
	long count;
	count = atomic_read(a);
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	printk(KERN_INFO "%s: count = %ld\n", DRIVER_NAME, count);
	ret = single_open(filp, proccount_show, (void *)count);
	if (!ret)
		atomic_inc(a);
	return ret;
}


static struct file_operations proccount_fops = {
	.owner = THIS_MODULE,
	.llseek = seq_lseek,
	.read = seq_read,
	.open = proccount_open,
	.release = single_release,
};

static int __init proccount_init(void)
{
	struct proc_dir_entry *pde;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	atomic_set(&proccount_counter, 0);
	pde = proc_create_data(proccount_file_name, PROCCOUNT_MODE, NULL,
			&proccount_fops, &proccount_counter);
	if (!pde) {
		printk(KERN_ERR "%s: proc_create_data failed\n", DRIVER_NAME);
		return -1;
	}
	printk(KERN_INFO "%s: initialized successfully\n", DRIVER_NAME);
	return 0;
}
module_init(proccount_init);

static void __exit proccount_exit(void)
{
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	remove_proc_entry(proccount_file_name, NULL);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(proccount_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A file in procfs that returns open count upon read");
MODULE_VERSION("1.0.1");
MODULE_LICENSE("GPL");

