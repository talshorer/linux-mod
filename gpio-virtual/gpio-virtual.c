#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#define MODULE_NAME "gpio-virtual"

static int vgpio_nchips = 1;
module_param_named(nchips, vgpio_nchips, int, 0444);
MODULE_PARM_DESC(nchips, "number of virtual gpio controller chips");

static int vgpio_chip_npins = 32;
module_param_named(chip_npins, vgpio_chip_npins, int, 0444);
MODULE_PARM_DESC(chip_npins, "number of gpio pins per controller chip");

static int __init vgpio_module_params(void) {
	int err = 0;
	if (vgpio_nchips <= 0) {
		pr_err("%s: vgpio_nchips <= 0. value = %d\n",
				MODULE_NAME, vgpio_nchips);
		err = -EINVAL;
	}
	if (vgpio_chip_npins <= 0) {
		pr_err("%s: vgpio_chip_npins <= 0. value = %d\n",
				MODULE_NAME, vgpio_chip_npins);
		err = -EINVAL;
	}
	/* vgpio_chip_npins must be a multiple of 8 */
	if (vgpio_chip_npins & 0x7) {
		pr_err("%s: vgpio_chip_npins is not a multiple of 8. "
				"value = %d\n", MODULE_NAME, vgpio_chip_npins);
		err = -EINVAL;
	}
	return err;
}

enum vgpio_reg_type {
	VGPIO_REG_VALUES,
	VGPIO_REG_DIRECTIONS,
	VGPIO_REG_TYPE_MAX
};
#define vgpio_reg_type_len (vgpio_chip_npins >> 3)

struct vgpio_chip {
	struct gpio_chip chip;
	spinlock_t lock; /* protects the virtual gpio controller registers */
	void *mem;
	/* virtual gpio controller registers */
	char *regs[VGPIO_REG_TYPE_MAX];
};
#define to_vgpio_chip(_chip) (container_of(_chip, struct vgpio_chip, chip))

static struct vgpio_chip *vgpio_chips;

#define VGPIO_MKDEV(i) MKDEV(0, i)

/* register manipulation functions */

static inline int vgpio_get_bit(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit)
{
	unsigned boffset = bit & 0x7;
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&vchip->lock, flags);
	ret = (vchip->regs[regtype][bit >> 3] & (1 << boffset)) >> boffset;
	spin_unlock_irqrestore(&vchip->lock, flags);
	return ret;
}

#define __vgpio_set_bit_op(vchip, regtype, bit, op) \
do { \
	unsigned __bit = (bit); \
	(vchip)->regs[regtype][__bit >> 3] op (1 << (__bit & 0x7)); \
} while (0)

static void __vgpio_set_bit_hi(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit)
{
	__vgpio_set_bit_op(vchip, regtype, bit, |=);
}

static void __vgpio_set_bit_lo(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit)
{
	__vgpio_set_bit_op(vchip, regtype, bit, &= ~);
}

static inline void vgpio_set_bit_hi(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit)
{
	unsigned long flags;
	spin_lock_irqsave(&vchip->lock, flags);
	__vgpio_set_bit_hi(vchip, regtype, bit);
	spin_unlock_irqrestore(&vchip->lock, flags);
}

static inline void vgpio_set_bit_lo(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit)
{
	unsigned long flags;
	spin_lock_irqsave(&vchip->lock, flags);
	__vgpio_set_bit_lo(vchip, regtype, bit);
	spin_unlock_irqrestore(&vchip->lock, flags);
}

static inline void __vgpio_set_bit(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit, int value)
{
	(value ? __vgpio_set_bit_hi : __vgpio_set_bit_lo)(vchip, regtype, bit);
}

static inline void vgpio_set_bit(struct vgpio_chip *vchip,
		enum vgpio_reg_type regtype, unsigned bit, int value)
{
	unsigned long flags;
	spin_lock_irqsave(&vchip->lock, flags);
	__vgpio_set_bit(vchip, regtype, bit, value);
	spin_unlock_irqrestore(&vchip->lock, flags);
}

/* gpio_chip operations */

static int vgpio_request(struct gpio_chip *chip, unsigned offset)
{
	dev_info(chip->dev, "<%s> offset = %u\n", __func__, offset);
	pm_runtime_get_sync(chip->dev);
	return 0;
}

static void vgpio_free(struct gpio_chip *chip, unsigned offset)
{
	dev_info(chip->dev, "<%s> offset = %u\n", __func__, offset);
	pm_runtime_put(chip->dev);
}

static int vgpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct vgpio_chip *vchip = to_vgpio_chip(chip);
	int ret = vgpio_get_bit(vchip, VGPIO_REG_DIRECTIONS, offset);
	dev_info(chip->dev, "<%s> offset = %u, ret = %d\n",
			__func__, offset, ret);
	return ret;
}

static int vgpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct vgpio_chip *vchip = to_vgpio_chip(chip);
	unsigned long flags;
	dev_info(chip->dev, "<%s> offset = %u\n", __func__, offset);
	spin_lock_irqsave(&vchip->lock, flags);
	__vgpio_set_bit_hi(vchip, VGPIO_REG_DIRECTIONS, offset);
	__vgpio_set_bit(vchip, VGPIO_REG_VALUES, offset, 0);
	spin_unlock_irqrestore(&vchip->lock, flags);
	return 0;
}

static int vgpio_direction_output(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct vgpio_chip *vchip = to_vgpio_chip(chip);
	unsigned long flags;
	dev_info(chip->dev, "<%s> offset = %u, value = %d\n",
			__func__, offset, value);
	spin_lock_irqsave(&vchip->lock, flags);
	__vgpio_set_bit_lo(vchip, VGPIO_REG_DIRECTIONS, offset);
	__vgpio_set_bit(vchip, VGPIO_REG_VALUES, offset, value);
	spin_unlock_irqrestore(&vchip->lock, flags);
	return 0;
}

static int vgpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct vgpio_chip *vchip = to_vgpio_chip(chip);
	int ret = vgpio_get_bit(vchip, VGPIO_REG_VALUES, offset);
	dev_info(chip->dev, "<%s> offset = %u, ret = %d\n",
			__func__, offset, ret);
	return ret;
}

static void vgpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct vgpio_chip *vchip = to_vgpio_chip(chip);
	dev_info(chip->dev, "<%s> offset = %u, value = %d\n",
			__func__, offset, value);
	vgpio_set_bit(vchip, VGPIO_REG_VALUES, offset, value);
}

#ifdef CONFIG_PM_RUNTIME
static int vgpio_runtime_suspend(struct device *dev)
{
	dev_info(dev, "<%s>\n", __func__);
	return 0;
}

static int vgpio_runtime_resume(struct device *dev)
{
	dev_info(dev, "<%s>\n", __func__);
	return 0;
}

static const struct dev_pm_ops __vgpio_pm_ops = {
	SET_RUNTIME_PM_OPS(vgpio_runtime_suspend, vgpio_runtime_resume, NULL)
};
#define vgpio_pm_ops &__vgpio_pm_ops
#else /* CONFIG_PM_RUNTIME */
#define vgpio_pm_ops NULL
#endif /* CONFIG_PM_RUNTIME */

static struct class vgpio_class = {
	.name = MODULE_NAME,
	.owner = THIS_MODULE,
	.pm = vgpio_pm_ops,
};

static int vgpio_chip_init(struct vgpio_chip *vchip, int i)
{
	int err;
	int j;

	spin_lock_init(&vchip->lock);
	vchip->chip.request = vgpio_request;
	vchip->chip.free = vgpio_free;
	vchip->chip.get_direction = vgpio_get_direction;
	vchip->chip.direction_input = vgpio_direction_input;
	vchip->chip.direction_output = vgpio_direction_output;
	vchip->chip.get = vgpio_get;
	vchip->chip.set = vgpio_set;
	vchip->chip.base = -1; /* request dynamic ID allocation */
	vchip->chip.ngpio = vgpio_chip_npins;

	vchip->mem = kzalloc(vgpio_reg_type_len * VGPIO_REG_TYPE_MAX,
			GFP_KERNEL);
	if (!vchip->mem) {
		err = -ENOMEM;
		pr_err("%s: <%s> i = %d, failed to allocate vchip->mem\n",
				MODULE_NAME, __func__, i);
		goto fail_kzalloc_vchip_mem;
	}
	for (j = 0; j < VGPIO_REG_TYPE_MAX; j++)
		vchip->regs[j] = vchip->mem + j * vgpio_reg_type_len;
	/* set all pins input */
	memset(vchip->regs[VGPIO_REG_DIRECTIONS], 0xff, vgpio_reg_type_len);

	vchip->chip.dev = device_create(&vgpio_class, NULL, VGPIO_MKDEV(i),
			vchip, "%s%d", MODULE_NAME, i);
	if (IS_ERR(vchip->chip.dev)) {
		err = PTR_ERR(vchip->chip.dev);
		pr_err("%s: <%s> i = %d, device_create failed. err = %d\n",
				MODULE_NAME, __func__, i, err);
		goto fail_device_create;
	}

	err = gpiochip_add(&vchip->chip);
	if (err) {
		pr_err("%s: <%s> i = %d, gpiochip_add failed. err = %d\n",
				MODULE_NAME, __func__, i, err);
		goto fail_gipochip_add;
	}

	pm_runtime_enable(vchip->chip.dev);

	pr_info("%s: created chip %s successfully\n",
			MODULE_NAME, dev_name(vchip->chip.dev));
	return 0;

fail_gipochip_add:
	device_destroy(&vgpio_class, VGPIO_MKDEV(i));
fail_device_create:
	kfree(vchip->mem);
fail_kzalloc_vchip_mem:
	return err;
}

static void vgpio_chip_cleanup(struct vgpio_chip *vchip)
{
	dev_t devt = vchip->chip.dev->devt;
	gpiochip_remove(&vchip->chip);
	device_destroy(&vgpio_class, devt);
	kfree(vchip->mem);
}

static int __init vgpio_init(void)
{
	int err;
	int i;

	err = vgpio_module_params();
	if (err)
		return err;

	vgpio_chips = vmalloc(sizeof(vgpio_chips[0]) * vgpio_nchips);
	if (!vgpio_chips) {
		err = -ENOMEM;
		pr_err("%s: failed to allocate vgpio_chips\n", MODULE_NAME);
		goto fail_vmalloc_vgpio_chips;
	}
	memset(vgpio_chips, 0, sizeof(vgpio_chips[0]) * vgpio_nchips);

	err = class_register(&vgpio_class);
	if (err) {
		pr_err("%s: class_create register. err = %d\n",
				MODULE_NAME, err);
		goto fail_class_create;
	}

	for (i = 0; i < vgpio_nchips; i++) {
		err = vgpio_chip_init(&vgpio_chips[i], i);
		if (err) {
			pr_err("%s: vgpio_chip_init failed. i = %d, "
					"err = %d\n", MODULE_NAME, i, err);
			goto fail_vgpio_chip_init_loop;
		}
	}

	pr_info("%s: initializated successfully\n", MODULE_NAME);
	return 0;

fail_vgpio_chip_init_loop:
	while (i--)
		vgpio_chip_cleanup(&vgpio_chips[i]);
	class_unregister(&vgpio_class);
fail_class_create:
	vfree(vgpio_chips);
fail_vmalloc_vgpio_chips:
	return err;
}
module_init(vgpio_init);

static void __exit vgpio_exit(void)
{
	int i;
	for (i = 0; i < vgpio_nchips; i++)
		vgpio_chip_cleanup(&vgpio_chips[i]);
	class_unregister(&vgpio_class);
	vfree(vgpio_chips);
	pr_info("%s: exited successfully\n", MODULE_NAME);
}
module_exit(vgpio_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virtual gpio controller chips");
MODULE_VERSION("1.1.1");
MODULE_LICENSE("GPL");
