#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include <lmod/meta.h>

static int firmreq_ndevices = 1;
module_param_named(ndevices, firmreq_ndevices, int, 0444);
MODULE_PARM_DESC(ndevices, "number of virtual devices to create");

static int __init firmreq_check_module_params(void) {
	int err = 0;
	if (firmreq_ndevices <= 0) {
		pr_err("firmreq_ndevices <= 0. value = %d\n",
				firmreq_ndevices);
		err = -EINVAL;
	}
	return err;
}

static ssize_t firmware_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct firmware *fw = dev_get_drvdata(dev);
	memcpy(buf, fw->data, fw->size);
	buf[fw->size] = '\n';
	buf[fw->size + 1] = '\0';
	return fw->size + 2;
}
static DEVICE_ATTR_RO(firmware);
static struct attribute *firmreq_dev_attrs[] = {
	&dev_attr_firmware.attr,
	NULL,
};
ATTRIBUTE_GROUPS(firmreq_dev);

static struct class *firmreq_class;

static struct device **firmreq_devices;

static struct device *firmreq_device_create(int i)
{
	int err;
	struct device *dev = NULL;
	char fw_name[32];
	const struct firmware *fw;

	dev = device_create(firmreq_class, NULL, MKDEV(0, i), NULL,
			"%s%d", KBUILD_MODNAME, i);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	sprintf(fw_name, "%s/%s.fw", KBUILD_MODNAME, dev_name(dev));
	err = request_firmware(&fw, fw_name, dev);
	if (err) {
		dev_err(dev, "request_firmware failed. i = %d, err = %d\n",
				i, err);
		goto fail_request_firmware;
	}
	dev_set_drvdata(dev, (void*)fw);

	dev_info(dev, "created successfully\n");
	return dev;

fail_request_firmware:
	device_unregister(dev);
fail_device_create:
	return ERR_PTR(err);
}

static void firmreq_device_destroy(struct device *dev)
{
	struct firmware *fw = dev_get_drvdata(dev);
	dev_info(dev, "destroying\n");
	release_firmware(fw);
	device_unregister(dev);
}

static int __init firmreq_init(void)
{
	int err;
	int i;

	err = firmreq_check_module_params();
	if (err)
		return err;

	firmreq_devices = vmalloc(
			sizeof(firmreq_devices[0]) * firmreq_ndevices);
	if (!firmreq_devices) {
		err = -ENOMEM;
		pr_err("failed to allocate firmreq_devices\n");
		goto fail_vmalloc_firmreq_devices;
	}
	memset(firmreq_devices, 0,
			sizeof(firmreq_devices[0]) * firmreq_ndevices);

	firmreq_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(firmreq_class)) {
		err = PTR_ERR(firmreq_class);
		pr_err("class_create failed. err = %d\n", err);
		goto fail_class_create;
	}
	firmreq_class->dev_groups = firmreq_dev_groups;

	for (i = 0; i < firmreq_ndevices; i++) {
		firmreq_devices[i] = firmreq_device_create(i);
		if (IS_ERR(firmreq_devices[i])) {
			err = PTR_ERR(firmreq_devices[i]);
			pr_err("firmreq_device_create. i = %d, err = %d\n",
					i, err);
			goto fail_firmreq_device_create_loop;
		}
	}

	pr_info("initializated successfully\n");
	return 0;

fail_firmreq_device_create_loop:
	while (i--)
		firmreq_device_destroy(firmreq_devices[i]);
	class_destroy(firmreq_class);
fail_class_create:
	vfree(firmreq_devices);
fail_vmalloc_firmreq_devices:
	return err;
}
module_init(firmreq_init);

static void __exit firmreq_exit(void)
{
	int i;
	for (i = 0; i < firmreq_ndevices; i++)
		firmreq_device_destroy(firmreq_devices[i]);
	class_destroy(firmreq_class);
	vfree(firmreq_devices);
	pr_info("exited successfully\n");
}
module_exit(firmreq_exit);


LMOD_MODULE_META();
MODULE_DESCRIPTION("Virtual devices that require firmware from userspace");
MODULE_VERSION("1.0.1");
