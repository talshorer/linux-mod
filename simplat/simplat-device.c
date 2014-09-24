#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "simplat.h"

#define DRIVER_NAME "simplat-device"

static int simplat_ndevices = 1;
module_param_named(ndevices, simplat_ndevices, int, 0444);
MODULE_PARM_DESC(ndevices, "number of simplat devices to create");

static int simplat_device_check_module_params(void) {
	int err = 0;

	if (simplat_ndevices < 0) {
		printk(KERN_ERR "%s: simplat_ndevices < 0. value = %d\n",
				DRIVER_NAME, simplat_ndevices);
		err = -EINVAL;
	}

	return err;
}

struct platform_device **simplat_devices;

static int __init simplat_device_init(void)
{
	int err;
	int i;

	err = simplat_device_check_module_params();
	if (err)
		return err;

	simplat_devices = vmalloc(sizeof(simplat_devices[0]) * simplat_ndevices);
	if (!simplat_devices) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate simplat_devices", DRIVER_NAME);
		goto fail_vmalloc_simplat_devices;
	}


	for (i = 0; i < simplat_ndevices; i++) {
		simplat_devices[i] = platform_device_alloc(SIMPLAT_PLATFORM_NAME, i);
		if (!simplat_devices[i]) {
			err = -ENOMEM;
			printk(KERN_ERR "%s: platform_device_alloc failed, i = %d\n",
					DRIVER_NAME, i);
			goto fail_platform_device_loop_alloc;
		}

		err = platform_device_add(simplat_devices[i]);
		if (err) {
			printk(KERN_ERR "%s: platform_device_add failed, i = %d, "
					"err = %d\n", DRIVER_NAME, i, err);
			goto fail_platform_device_loop_add;
		}
	}

	return 0;

fail_platform_device_loop_add:
	platform_device_put(simplat_devices[i]);
fail_platform_device_loop_alloc:
	while (i--)
		platform_device_unregister(simplat_devices[i]);
	vfree(simplat_devices);
fail_vmalloc_simplat_devices:
	return err;
}
module_init(simplat_device_init);

static void __exit simplat_device_exit(void)
{
	int i;
	for (i = 0; i < simplat_ndevices; i++)
		platform_device_unregister(simplat_devices[i]);
	vfree(simplat_devices);
}
module_exit(simplat_device_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Spawns devices for the simplat platform driver");
MODULE_VERSION("1.0.1");
MODULE_LICENSE("GPL");
