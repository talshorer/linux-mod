#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/binfmts.h>

#define BRAINFUCK_SUFFIX "bf"

static char *brainfuck_interpreter;
module_param_named(interpreter, brainfuck_interpreter, charp, S_IRUGO);
MODULE_PARM_DESC(interpreter, "path to the brainfuck interpreter");

/* declare this so it can be used for set_binfmt() */
static struct linux_binfmt brainfuck_format;

/* based on load_script in fs/binfmt_script.c */
static int load_brainfuck_binary(struct linux_binprm *bprm)
{
	char *p;
	const char *interp; /* used because of type checking  */
	int ret;
	char filepath[256];

	p = strrchr(bprm->filename, '.');
	if (!p || strcmp(p + 1, BRAINFUCK_SUFFIX))
		return -ENOEXEC;

	p = d_path(&bprm->file->f_path, filepath, sizeof(filepath));
	if (IS_ERR(p))
		p = "(error)";
	pr_info("<%s> path = \"%s\"\n", __func__, p);

	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	ret = remove_arg_zero(bprm);
	if (ret)
		return ret;

	/*
	 * Args are done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */

	ret = copy_strings_kernel(1, &bprm->filename, bprm);
	if (ret)
		return ret;
	bprm->argc++;

	interp = brainfuck_interpreter;
	ret = copy_strings_kernel(1, &interp, bprm);
	if (ret)
		return ret;
	bprm->argc++;
	ret = bprm_change_interp(brainfuck_interpreter, bprm);
	if (ret)
		return ret;

	bprm->file = open_exec(brainfuck_interpreter);
	if (IS_ERR(bprm->file)) {
		ret = PTR_ERR(bprm->file);
		bprm->file = NULL;
		return ret;
	}

	ret = prepare_binprm(bprm);
	if (ret < 0)
		return ret;

	return search_binary_handler(bprm);
}

static struct linux_binfmt brainfuck_format = {
	.module       = THIS_MODULE,
	.load_binary  = load_brainfuck_binary,
};

static int __init binfmt_brainfuck_init(void)
{
	if (!brainfuck_interpreter) {
		pr_err("interpreter not provided\n");
		return -EINVAL;
	}
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
MODULE_VERSION("1.0.1");
MODULE_LICENSE("GPL");
