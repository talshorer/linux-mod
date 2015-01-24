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

struct jrtc_time {
	unsigned long time;
	unsigned long jiffies; /* typeof(jiffies) */
};

struct jrtc_clock {
	struct device *dev;
	struct rtc_device *rtc;
	struct jrtc_time last_set;
	spinlock_t lock; /* protects last_set */
};

#define to_jrtc_clock(dev) ((struct jrtc_clock *)dev_get_drvdata(dev))

static int jrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct jrtc_clock *jc = to_jrtc_clock(dev);
	struct jrtc_time jt;
	unsigned long flags;

	dev_info(dev, "<%s>\n", __func__);

	spin_lock_irqsave(&jc->lock, flags);
	memcpy(&jt, &jc->last_set, sizeof(jt));
	spin_unlock_irqrestore(&jc->lock, flags);

	rtc_time_to_tm(jt.time + (jiffies - jt.jiffies) / HZ, tm);

	return 0;
}

static int jrtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct jrtc_clock *jc = to_jrtc_clock(dev);
	struct jrtc_time jt;
	unsigned long flags;
	int err;

	dev_info(dev, "<%s>\n", __func__);

	err = rtc_tm_to_time(tm, &jt.time);
	if (err) {
		dev_err(dev, "rtc_tm_to_time failed, err = %d\n", err);
		return err;
	}
	jt.jiffies = jiffies;

	spin_lock_irqsave(&jc->lock, flags);
	memcpy(&jc->last_set, &jt, sizeof(jt));
	spin_unlock_irqrestore(&jc->lock, flags);

	return 0;
}

static const struct rtc_class_ops jrtc_rtc_ops = {
	.read_time	= jrtc_read_time,
	.set_time	= jrtc_set_time,
};

static struct jrtc_clock *jrtc_clocks;

static struct class *jrtc_class;
#define JRTC_MKDEV(i) MKDEV(0, i)

static int jrtc_clock_setup(struct jrtc_clock *jc, int i)
{
	int err;

	jc->last_set.jiffies = jiffies;
	spin_lock_init(&jc->lock);

	jc->dev = device_create(jrtc_class, NULL, JRTC_MKDEV(i), jc,
			"%s%d", jrtc_class->name, i);
	if (IS_ERR(jc->dev)) {
		err = PTR_ERR(jc->dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	jc->rtc = rtc_device_register(dev_name(jc->dev), jc->dev,
			&jrtc_rtc_ops, THIS_MODULE);
	if (IS_ERR(jc->rtc)) {
		err = PTR_ERR(jc->rtc);
		pr_err("rtc_device_register failed. i = %d, err = %d\n",
				i, err);
		goto fail_rtc_device_register;
	}

	dev_info(jc->dev, "created successfully\n");
	return 0;

fail_rtc_device_register:
	device_destroy(jrtc_class, jc->dev->devt);
fail_device_create:
	return err;
}

static void jrtc_clock_cleanup(struct jrtc_clock *jc)
{
	dev_info(jc->dev, "destroying\n");
	rtc_device_unregister(jc->rtc);
	device_destroy(jrtc_class, jc->dev->devt);
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
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
