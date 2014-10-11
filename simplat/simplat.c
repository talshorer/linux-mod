#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "simplat.h"

/* not needed */
/* #define DRIVER_NAME "simplat" */

static int simplat_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "<%s>\n", __func__);
	return 0;
}

static int simplat_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "<%s>\n", __func__);
	return 0;
}

static struct platform_driver simplat_driver = {
	.driver = {
		.name		= SIMPLAT_PLATFORM_NAME,
		.owner		= THIS_MODULE,
	},
	.probe		= simplat_probe,
	.remove		= simplat_remove,
};

module_platform_driver(simplat_driver);

MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A simple platform driver that does nothing");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
