#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serio.h>

#include <lmod/meta.h>

#include "seriorepeater_uapi.h"

static int seriorepeater_connect(struct serio *serio, struct serio_driver *drv)
{
	int err;

	dev_info(&serio->dev, "<%s>\n", __func__);
	err = serio_open(serio, drv);
	if (err)
		dev_err(&serio->dev, "serio_open failed with err %d\n", err);
	return err;
}

static void seriorepeater_disconnect(struct serio *serio)
{
	dev_info(&serio->dev, "<%s>\n", __func__);
	serio_close(serio);
}

static irqreturn_t seriorepeater_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	dev_info(&serio->dev, "<%s> data 0x%02x, flags 0x%08x\n", __func__,
			data, flags);
	serio_write(serio, data);
	return IRQ_HANDLED;
}

static struct serio_device_id seriorepeater_serio_ids[] = {
	{
		.type	= SERIO_ANY,
		.proto	= SERIO_REPEATER,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ /* terminating entry */ }
};

MODULE_DEVICE_TABLE(serio, seriorepeater_serio_ids);

static struct serio_driver seriorepeater_drv = {
	.driver		= {
		.name	= "seriorepeater",
	},
	.description	= "serio repeater",
	.id_table	= seriorepeater_serio_ids,
	.connect	= seriorepeater_connect,
	.disconnect	= seriorepeater_disconnect,
	.interrupt	= seriorepeater_interrupt,
};

module_serio_driver(seriorepeater_drv);


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A repeater on the serio bus");
MODULE_VERSION("0.0.1");
