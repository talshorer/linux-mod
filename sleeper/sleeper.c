#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/vmalloc.h>

#include <lmod/meta.h>

#define DRIVER_NAME "sleeper"

static int sleeper_nthreads = 1;
module_param_named(nthreads, sleeper_nthreads, int, 0444);
MODULE_PARM_DESC(nthreads, "number of sleeper threads to create");

static int sleeper_check_module_params(void)
{
	int err = 0;

	if (sleeper_nthreads < 0) {
		pr_err("sleeper_nthreads < 0. value = %d\n", sleeper_nthreads);
		err = -EINVAL;
	}

	return err;
}

struct sleeper_thread {
	struct task_struct *task;
	wait_queue_head_t wq;
	atomic_t disturbs;
};

static struct sleeper_thread *sleeper_threads;

static struct dentry *sleeper_debugfs;

static int sleeper_debugfs_wake_open_release(struct inode *inode,
		struct file *filp)
	{ return 0; }

static ssize_t sleeper_debugfs_wake_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char kbuf[16];
	unsigned int id;

	memset(kbuf, 0x00, sizeof(kbuf));
	count = min_t(size_t, sizeof(kbuf) - 1, count);

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	if (sscanf(kbuf, "%u\n", &id) != 1) {
		pr_err("<%s> expected a number\n", __func__);
		return -EINVAL;
	}
	if (id >= sleeper_nthreads) {
		pr_err("<%s> id >= nthreads. id = %d, nthreads = %d\n",
				__func__, id, sleeper_nthreads);
		return -EINVAL;
	}

	wake_up(&sleeper_threads[id].wq);

	return count;
}

#define sleeper_debugfs_wake_fname "wake"
static const struct file_operations sleeper_debugfs_wake_fops = {
	.open = sleeper_debugfs_wake_open_release,
	.release = sleeper_debugfs_wake_open_release,
	.write = sleeper_debugfs_wake_write,
};

static int sleeper_debugfs_stat_show(struct seq_file *m, void *v)
{
	int i;
	struct sleeper_thread *st;

	for (i = 0; i < sleeper_nthreads; i++) {
		st = &sleeper_threads[i];
		seq_printf(m, "%s: %d\n", st->task->comm,
				atomic_read(&st->disturbs));
	}

	return 0;
}

static int sleeper_debugfs_stat_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sleeper_debugfs_stat_show, inode->i_private);
}

#define sleeper_debugfs_stat_fname "stats"
static const struct file_operations sleeper_debugfs_stat_fops = {
	.open = sleeper_debugfs_stat_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int __init sleeper_create_debugfs(void)
{
	int err;
	struct dentry *file;

	sleeper_debugfs = debugfs_create_dir(DRIVER_NAME, NULL);
	if (!sleeper_debugfs) {
		err = -ENOMEM;
		pr_err("debugfs_create_dir failed\n");
		goto fail_debugfs_create_dir;
	}

	file = debugfs_create_file(sleeper_debugfs_wake_fname, S_IWUSR,
			sleeper_debugfs, sleeper_threads,
			&sleeper_debugfs_wake_fops);
	if (!file) {
		err = -ENOMEM;
		pr_err("debugfs_create_file failed for %s\n",
				sleeper_debugfs_wake_fname);
		goto fail_debugfs_create_file;
	}

	file = debugfs_create_file(sleeper_debugfs_stat_fname, 0444,
			sleeper_debugfs, sleeper_threads,
			&sleeper_debugfs_stat_fops);
	if (!file) {
		err = -ENOMEM;
		pr_err("debugfs_create_file failed for %s\n",
				sleeper_debugfs_stat_fname);
		goto fail_debugfs_create_file;
	}

	return 0;

fail_debugfs_create_file:
	debugfs_remove_recursive(sleeper_debugfs);
fail_debugfs_create_dir:
	return err;
}

static int sleeper_thread_func(void *data)
{
	struct sleeper_thread *st = data;

	do {
		DEFINE_WAIT(wait);

		pr_info("%s was disturbed %d times\n", st->task->comm,
				atomic_read(&st->disturbs));
		prepare_to_wait(&st->wq, &wait, TASK_UNINTERRUPTIBLE);
		if (!kthread_should_stop())
			freezable_schedule();
		finish_wait(&st->wq, &wait);
		atomic_inc(&st->disturbs);
	} while (!kthread_should_stop());
	pr_notice("%s is shutting down\n", st->task->comm);
	return 0;
}

static int __init sleeper_thread_setup(struct sleeper_thread *st,
		unsigned int i)
{
	int err;

	init_waitqueue_head(&st->wq);
	atomic_set(&st->disturbs, 0);

	st->task = kthread_run(sleeper_thread_func, st, "%s%d",
			DRIVER_NAME, i);
	if (IS_ERR(st->task)) {
		err = PTR_ERR(st->task);
		pr_err("kthread_run failed, i = %d, err = %d\n", i, err);
		goto fail_kthread_run;
	}

	return 0;

fail_kthread_run:
	return err;
}

static inline void sleeper_thread_cleanup(struct sleeper_thread *st)
{
	kthread_stop(st->task);
}

static int __init sleeper_init(void)
{
	int err;
	int i;

	err = sleeper_check_module_params();
	if (err)
		return err;

	sleeper_threads = vmalloc(
			sizeof(sleeper_threads[0]) * sleeper_nthreads);
	if (!sleeper_threads) {
		err = -ENOMEM;
		pr_err("failed to allocate sleeper_threads\n");
		goto fail_vmalloc_sleeper_threads;
	}

	err = sleeper_create_debugfs();
	if (err)
		goto fail_sleeper_create_debugfs;

	for (i = 0; i < sleeper_nthreads; i++) {
		err = sleeper_thread_setup(&sleeper_threads[i], i);
		if (err) {
			pr_err(
			"sleeper_thread_setup failed. i = %d, err = %d\n", i,
					err);
			goto fail_sleeper_thread_setup_loop;
		}
	}

	pr_info("initializated successfully\n");
	return 0;

fail_sleeper_thread_setup_loop:
	while (i--)
		sleeper_thread_cleanup(&sleeper_threads[i]);
	debugfs_remove_recursive(sleeper_debugfs);
fail_sleeper_create_debugfs:
	vfree(sleeper_threads);
fail_vmalloc_sleeper_threads:
	return err;
}
module_init(sleeper_init);

static void __exit sleeper_exit(void)
{
	int i;

	for (i = 0; i < sleeper_nthreads; i++)
		sleeper_thread_cleanup(&sleeper_threads[i]);
	debugfs_remove_recursive(sleeper_debugfs);
	vfree(sleeper_threads);

	pr_info("exited successfully\n");
}
module_exit(sleeper_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("Kernel threads that sleep until woken up by user");
MODULE_VERSION("1.0.5");
