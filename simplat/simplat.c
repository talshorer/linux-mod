#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <lmod/meta.h>

#include "simplat.h"

static void simplat_dummy_release(struct device *dev, void *res)
{
	dev_info(dev, "<%s>\n", __func__);
}

static int simplat_probe(struct platform_device *pdev)
{
	void *res;

	dev_info(&pdev->dev, "<%s>\n", __func__);

	res = devres_alloc(simplat_dummy_release, 0, GFP_KERNEL);
	if (!res) {
		dev_err(&pdev->dev, "devres_alloc failed\n");
		return -ENOMEM;
	}
	devres_add(&pdev->dev, res);

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

LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("A simple platform driver that does nothing");
MODULE_VERSION("1.1.0");
