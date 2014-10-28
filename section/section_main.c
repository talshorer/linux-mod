#include <linux/module.h>
#include <linux/kernel.h>

#define MODULE_NAME "section"

typedef char *(*section_dummy)(void);

asm (".section .dummies, \"aw\""); \
extern section_dummy __start_dummies; \
extern section_dummy __stop_dummies;

#define section_define_dummy_func(name) \
	static char *name(void) { return #name; } \
	static __attribute__((__used__)) __attribute__((__section__(".dummies"))) \
			section_dummy __dummies__##name = &name;

section_define_dummy_func(foo);
section_define_dummy_func(bar);

static int __init section_init(void)
{
	section_dummy *fp;
	for (fp = &__start_dummies; fp < &__stop_dummies; fp++)
		pr_info("%s: %p %s\n", MODULE_NAME, fp, (*fp)());
	return 0;
}
module_init(section_init);

static void __exit section_exit(void)
{
	pr_info("%s: exitting\n", MODULE_NAME);
}
module_exit(section_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A module with a section of dummy function pointers");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
