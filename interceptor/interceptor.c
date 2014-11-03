#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/utsname.h>
#include <asm/syscall.h>

#define MODULE_NAME "interceptor"

#define INTERCEPTOR_SYSMAP_FILE_PREFIX "/boot/System.map-"
#define INTERCEPTOR_SYSCALL_TABLE_SYMBOL "sys_call_table"

static sys_call_ptr_t *interceptor_sys_call_table;

/*
 * all this trouble for
 * grep "\bsys_call_table\b" /boot/System.map-$(uname -r) | awk '{print $1}'
 */
static sys_call_ptr_t *interceptor_get_syscall_table(void)
{
	int err = 0;
	int i;
	struct new_utsname *uname;
	char sysmap_filename[sizeof(uname->release) + \
			sizeof(INTERCEPTOR_SYSMAP_FILE_PREFIX) - 1];
	char buf[128]; /* should be able to contain any line in the sysmap file */
	char symbol[128]; /* should be able to conatin the name of any symbol */
	unsigned long addr;
	char dummy;
	struct file *filp;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(get_ds());

	uname = utsname();
	sprintf(sysmap_filename, INTERCEPTOR_SYSMAP_FILE_PREFIX "%s",
			uname->release);
	filp = filp_open(sysmap_filename, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);
		pr_err("%s: <%s> filp_open failed, err = %d\n",
				MODULE_NAME, __func__, err);
		goto out_none;
	}

	while (1) {
		memset(buf, 0, sizeof(buf));
		err = vfs_read(filp, (char __user *)buf, sizeof(buf), &filp->f_pos);
		if (err < 0) {
			pr_err("%s: <%s> vfs_read failed, err = %d\n",
					MODULE_NAME, __func__, err);
			goto out_close;
		}
		/* vfs_read returned 0, reached EOF */
		if (!err) {
			err = -EINVAL;
			pr_err("%s: <%s> reached end of file. should never happen!\n",
					MODULE_NAME, __func__);
			goto out_close;
		}
		for (i = 0; i < sizeof(buf); i++)
			if (buf[i] == '\n') {
				buf[i] = 0;
				break;
			}
		if (i == sizeof(buf)) {
			pr_err("%s: <%s> buf not big enough to hold a line\n",
					MODULE_NAME, __func__);
			err = -ENOMEM;
			goto out_close;
		}
		/* 1 after the newline */
		vfs_llseek(filp, i - sizeof(buf) + 1, SEEK_CUR);
		if (sscanf(buf, "%lx %c %s", &addr, &dummy, symbol) != 3) {
			pr_err("%s: <%s> sscanf returned unexpected value\n",
					MODULE_NAME, __func__);
			pr_err("%s: <%s> line was \"%s\"\n", MODULE_NAME, __func__, buf);
			err = -EINVAL;
			goto out_close;
		}
		if (!strcmp(symbol, INTERCEPTOR_SYSCALL_TABLE_SYMBOL)) {
			pr_info("%s: <%s> found %s 0x%lx\n", MODULE_NAME, __func__,
					INTERCEPTOR_SYSCALL_TABLE_SYMBOL, addr);
			break;
		}
	}

out_close:
	filp_close(filp, NULL);
out_none:
	set_fs(oldfs);
	if (err)
		return ERR_PTR(err);
	return (sys_call_ptr_t *)addr;
}

static int __init interceptor_init(void)
{
	int err;

	interceptor_sys_call_table = interceptor_get_syscall_table();
	if (IS_ERR(interceptor_sys_call_table)) {
		err = PTR_ERR(interceptor_sys_call_table);
		goto fail_get_syscall_table;
	}

	pr_info("%s: initialized successfully\n", MODULE_NAME);
	return 0;

fail_get_syscall_table:
	return err;
}
module_init(interceptor_init);

static void __exit interceptor_exit(void)
{
	pr_info("%s: exited successfully\n", MODULE_NAME);
}
module_exit(interceptor_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Intercepts a system call");
MODULE_VERSION("0.1.1");
MODULE_LICENSE("GPL");
