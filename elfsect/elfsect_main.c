#include <linux/module.h>
#include <linux/kernel.h>

#define MODULE_NAME "elfsect"

typedef char *(*elfsect_dummy)(void);

extern elfsect_dummy __start_dummies;
extern elfsect_dummy __stop_dummies;

#define elfsect_define_dummy_func(name) \
	static char *name(void) { return #name; } \
	static __attribute__((__used__)) __attribute__((__section__(".dummies"))) \
			elfsect_dummy __dummies__##name = &name;

elfsect_define_dummy_func(foo);
elfsect_define_dummy_func(bar);

static int __init elfsect_init(void)
{
	elfsect_dummy *fp;
	for (fp = &__start_dummies; fp < &__stop_dummies; fp++)
		pr_info("%s: %p %s\n", MODULE_NAME, fp, (*fp)());
	return 0;
}
module_init(elfsect_init);

static void __exit elfsect_exit(void)
{
	pr_info("%s: exitting\n", MODULE_NAME);
}
module_exit(elfsect_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A module with a section of dummy function pointers");
MODULE_VERSION("1.0.2");
MODULE_LICENSE("GPL");
