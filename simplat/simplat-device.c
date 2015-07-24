#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>

#include <lmod/meta.h>

#include "simplat.h"

static int simplat_ndevices = 1;
module_param_named(ndevices, simplat_ndevices, int, 0444);
MODULE_PARM_DESC(ndevices, "number of simplat devices to create");

static int simplat_device_check_module_params(void)
{
	int err = 0;

	if (simplat_ndevices < 0) {
		pr_err("simplat_ndevices < 0. value = %d\n", simplat_ndevices);
		err = -EINVAL;
	}

	return err;
}

static struct platform_device **simplat_devices;

static int __init simplat_device_init(void)
{
	int err;
	int i;

	err = simplat_device_check_module_params();
	if (err)
		return err;

	simplat_devices = vmalloc(
			sizeof(simplat_devices[0]) * simplat_ndevices);
	if (!simplat_devices) {
		err = -ENOMEM;
		pr_err("failed to allocate simplat_devices");
		goto fail_vmalloc_simplat_devices;
	}


	for (i = 0; i < simplat_ndevices; i++) {
		simplat_devices[i] = platform_device_alloc(
				SIMPLAT_PLATFORM_NAME, i);
		if (!simplat_devices[i]) {
			err = -ENOMEM;
			pr_err(
			"platform_device_alloc failed, i = %d\n", i);
			goto fail_platform_device_loop_alloc;
		}

		err = platform_device_add(simplat_devices[i]);
		if (err) {
			pr_err(
			"platform_device_add failed, i = %d, err = %d\n", i,
					err);
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


LMOD_MODULE_AUTHOR();
LMOD_MODULE_LICENSE();
MODULE_DESCRIPTION("Spawns devices for the simplat platform driver");
MODULE_VERSION("1.0.3");
