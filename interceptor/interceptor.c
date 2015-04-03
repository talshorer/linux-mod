#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/utsname.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include <asm/syscall.h>

#include <lmod/meta.h>

#include "interceptor_uapi.h"

#define MODULE_NAME "interceptor"

#define INTERCEPTOR_SYSCALL_TABLE_SYMBOL "sys_call_table"

static sys_call_ptr_t *interceptor_sys_call_table;
static sys_call_ptr_t interceptor_orig_syscall_ptr;

/*
 * keep the parameters for this function compatible with the system call
 * being intercepted
 */
#define INTERCEPTOR_SYSCALL_NR __NR_open
asmlinkage int interceptor_syscall(const char *pathname, int flags,
		umode_t mode)
{
	int ret;

	if (flags & INTERCEPTOR_O_STRLEN) {
		ret = strlen(pathname);
		pr_info("%s: intercepted open() call! "
				"returning strlen(\"%s\")\n",
				MODULE_NAME, pathname);
	} else {
		ret = ((typeof(interceptor_syscall) *)
				interceptor_orig_syscall_ptr)(
						pathname, flags, mode);
		pr_info("%s: <%s> pid %d, args %s 0x%x 0x%x, ret %d\n",
				MODULE_NAME, __func__, current->pid, pathname,
				flags, mode, ret);
	}
	return ret;
}

#define ptep_val_dref(ptep) (*(unsigned long *)ptep)

static sys_call_ptr_t interceptor_swap_syscalls(
		sys_call_ptr_t *table, unsigned int nr, sys_call_ptr_t value)
{
	unsigned long addr = (unsigned long)&table[nr];
	unsigned int level;
	int ro;
	pte_t *ptep;

	ptep = lookup_address(addr, &level);
	ro = !(ptep_val_dref(ptep) & _PAGE_RW);
	if (ro)
		ptep_val_dref(ptep) |= _PAGE_RW;
	value = xchg((sys_call_ptr_t *)addr, value);
	if (ro)
		ptep_val_dref(ptep) &= ~_PAGE_RW;

	return value;
}

static int __init interceptor_init(void)
{
	int err;
	unsigned long addr;

	addr = kallsyms_lookup_name(INTERCEPTOR_SYSCALL_TABLE_SYMBOL);
	if (!addr) {
		err = -EINVAL;
		pr_err("%s: kallsyms_lookup_name failed\n", MODULE_NAME);
		goto fail_get_syscall_table;
	}
	interceptor_sys_call_table = (sys_call_ptr_t *)addr;

	interceptor_orig_syscall_ptr = interceptor_swap_syscalls(
			interceptor_sys_call_table, INTERCEPTOR_SYSCALL_NR,
			(sys_call_ptr_t)interceptor_syscall);

	pr_info("%s: initialized successfully\n", MODULE_NAME);
	return 0;

fail_get_syscall_table:
	return err;
}
module_init(interceptor_init);

static void __exit interceptor_exit(void)
{
	interceptor_swap_syscalls(interceptor_sys_call_table,
			INTERCEPTOR_SYSCALL_NR, interceptor_orig_syscall_ptr);
	pr_info("%s: exited successfully\n", MODULE_NAME);
}
module_exit(interceptor_exit);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("Intercepts a system call");
MODULE_VERSION("1.1.1");
