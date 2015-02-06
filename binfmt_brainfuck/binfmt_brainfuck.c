#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/binfmts.h>

/* declare this so it can be used for set_binfmt() */
static struct linux_binfmt brainfuck_format;

static int load_brainfuck_binary(struct linux_binprm * bprm)
{
	pr_info("<%s>\n", __func__);
	return -ENOEXEC;
}

static struct linux_binfmt brainfuck_format = {
	.module       = THIS_MODULE,
	.load_binary  = load_brainfuck_binary,
};

static int __init binfmt_brainfuck_init(void)
{
	register_binfmt(&brainfuck_format);
	return 0;
}
module_init(binfmt_brainfuck_init);

static void __exit binfmt_brainfuck_exit(void)
{
	unregister_binfmt(&brainfuck_format);
}
module_exit(binfmt_brainfuck_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Binary interpreter for brainfuck files");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");
