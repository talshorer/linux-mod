#include <linux/module.h>
#include <linux/kernel.h>

#include "elfsect.h"

#define MODULE_NAME "elfsect"

extern elfsect_dummy __start_dummies;
extern elfsect_dummy __stop_dummies;

static int __init elfsect_init(void)
{
	elfsect_dummy *fp;
	pr_info("%s: initializing\n", MODULE_NAME);
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
MODULE_VERSION("1.1.0");
MODULE_LICENSE("GPL");
