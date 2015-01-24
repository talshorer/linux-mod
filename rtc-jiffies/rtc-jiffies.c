#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rtc.h>

static int jrtc_nclocks = 1;
module_param_named(nclocks, jrtc_nclocks, int, 0444);
MODULE_PARM_DESC(nclocks, "number of virtual clocks to create");

static int __init jrtc_check_module_params(void) {
	int err = 0;
	if (jrtc_nclocks <= 0) {
		pr_err("jrtc_nclocks <= 0. value = %d\n", jrtc_nclocks);
		err = -EINVAL;
	}
	return err;
}

static int jrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	/* TODO use rtc_time_to_tm */
	return -ENODEV;
}

static int jrtc_set_time(struct device *dev, struct rtc_time *tm)
{
	/* TODO use rtc_tm_time */
	return -ENODEV;
}

static const struct rtc_class_ops jrtc_rtc_ops = {
	.read_time	= jrtc_read_time,
	.set_time	= jrtc_set_time,
};

struct jrtc_clock {
	struct device *dev;
	struct rtc_device *rtc;
	unsigned long last_set_time;
	unsigned long last_set_jiffies; /* typeof(jiffies) */
};

static struct jrtc_clock *jrtc_clocks;

static struct class *jrtc_class;

static int jrtc_clock_setup(struct jrtc_clock *jc, int i)
{
	/* TODO */
	return 0;
}

static void jrtc_clock_cleanup(struct jrtc_clock *jc)
{
	/* TODO */
}

static int __init jrtc_init(void)
{
	int err;
	int i;

	err = jrtc_check_module_params();
	if (err)
		return err;

	jrtc_clocks = vmalloc(sizeof(jrtc_clocks[0]) * jrtc_nclocks);
	if (!jrtc_clocks) {
		err = -ENOMEM;
		pr_err("failed to allocate jrtc_clocks\n");
		goto fail_vmalloc_jrtc_clocks;
	}
	memset(jrtc_clocks, 0, sizeof(jrtc_clocks[0]) * jrtc_nclocks);

	jrtc_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(jrtc_class)) {
		err = PTR_ERR(jrtc_class);
		pr_err("class_create failed. err = %d\n", err);
		goto fail_class_create;
	}

	for (i = 0; i < jrtc_nclocks; i++) {
		err = jrtc_clock_setup(&jrtc_clocks[i], i);
		if (err) {
			pr_err("jrtc_clock_setup failed. i = %d, err=%d\n",
					i, err);
			goto fail_jrtc_clock_setup_loop;
		}
	}

	pr_info("initializated successfully\n");
	return 0;

fail_jrtc_clock_setup_loop:
	while (i--)
		jrtc_clock_cleanup(&jrtc_clocks[i]);
	class_destroy(jrtc_class);
fail_class_create:
	vfree(jrtc_clocks);
fail_vmalloc_jrtc_clocks:
	return err;
}
module_init(jrtc_init);

static void __exit jrtc_exit(void)
{
	int i;
	for (i = 0; i < jrtc_nclocks; i++)
		jrtc_clock_cleanup(&jrtc_clocks[i]);
	class_destroy(jrtc_class);
	vfree(jrtc_clocks);
	pr_info("exited successfully\n");
}
module_exit(jrtc_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virtual real time clocks that work on system ticks");
MODULE_VERSION("0.1.0");
MODULE_LICENSE("GPL");
