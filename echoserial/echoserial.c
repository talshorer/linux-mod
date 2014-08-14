#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include <linux/kfifo.h>

static const char DRIVER_NAME[] = "echoserial";

typedef STRUCT_KFIFO_PTR(char) echoserial_fifo_t;

struct echoserial_port {
	struct uart_port port;
	struct device *dev;
	echoserial_fifo_t fifo;
};

#define echoserial_port_devno(esp) ((esp)->dev->devt)
#define echoserial_port_kobj(esp) (&(esp)->dev->kobj)
#define echoserial_port_name(esp) (echoserial_port_kobj(esp)->name)

static int echoserial_nports = 1;
module_param_named(nports, echoserial_nports, int, 0444);
MODULE_PARM_DESC(nports, "number of ports to create");

static int echoserial_bsize = PAGE_SIZE;
module_param_named(bsize, echoserial_bsize, int, 0444);
MODULE_PARM_DESC(bsize, "size (in bytes) for each port's fifo");

static int __init echoserial_check_module_params(void) {
	int err = 0;
	if (echoserial_nports < 0) {
		printk(KERN_ERR "%s: echoserial_nports < 0. value = %d\n",
				DRIVER_NAME, echoserial_nports);
		err = -EINVAL;
	}
	if (echoserial_bsize < 0) {
		printk(KERN_ERR "%s: echoserial_bsize < 0. value = 0x%x\n",
				DRIVER_NAME, echoserial_bsize);
		err = -EINVAL;
	}
	/* echoserial_bsize must be a power of two */
	if (echoserial_bsize & (echoserial_bsize -1)) {
		printk(KERN_ERR "%s: echoserial_bsize is not a power of two. value = %d\n",
				DRIVER_NAME, echoserial_bsize);
		err = -EINVAL;
	}
	return err;
}


static const char echoserial_devname[] = "ttyE";

static struct echoserial_port *echoserial_ports;

static struct uart_driver echoserial_driver = {
	.owner = THIS_MODULE,
	.driver_name = DRIVER_NAME,
	.dev_name = echoserial_devname,
	/* .nr set during init */
};

static struct class *echoserial_class;

/* TODO port ops */

static struct uart_ops echoserial_uart_ops = {

};

/* TODO port setup/cleanup */

static int __init echoserial_port_setup(struct echoserial_port *esp, int i)
{
	struct uart_port *up = &esp->port;
	struct device *dev;
	int err;

	err = kfifo_alloc(&esp->fifo, echoserial_bsize, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "%s: kfifo_alloc failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_kfifo_alloc;
	}

	dev = device_create(echoserial_class, NULL,
			MKDEV(echoserial_driver.major, echoserial_driver.minor + i),
			esp, "%s%d", DRIVER_NAME, i);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		printk(KERN_ERR "%s: device_create failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_device_create;
	}
	esp->dev = dev;

	up->ops = &echoserial_uart_ops;
	up->dev = dev;
	up->line = i;
	/* TODO uart_add_one_port() */

	printk(KERN_INFO "%s: created port %s successfully\n",
			DRIVER_NAME, echoserial_port_name(esp));

	return 0;
fail_device_create:
	kfifo_free(&esp->fifo);
fail_kfifo_alloc:
	return err;
}

static void echoserial_port_cleanup(struct echoserial_port *esp)
{
	printk(KERN_INFO "%s: destroying port %s\n", DRIVER_NAME,
			echoserial_port_name(esp));
	device_destroy(echoserial_class, echoserial_port_devno(esp));
	kfifo_free(&esp->fifo);
}

static int __init echoserial_init(void)
{
	int err;
	int i;

	err = echoserial_check_module_params();
	if (err)
		return err;

	echoserial_ports = vmalloc(
			sizeof(echoserial_ports[0]) * echoserial_nports);
	if (!echoserial_ports) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate echoserial_ports\n",
				DRIVER_NAME);
		goto fail_vmalloc_echoserial_ports;
	}

	echoserial_driver.nr = echoserial_nports;
	err = uart_register_driver(&echoserial_driver);
	if (err) {
		printk(KERN_ERR "%s: uart_register_driver failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_uart_register_driver;
	}
	echoserial_driver.major = echoserial_driver.tty_driver->major;
	echoserial_driver.minor = echoserial_driver.tty_driver->minor_start;

	echoserial_class = class_create(THIS_MODULE, DRIVER_NAME);
	if (IS_ERR(echoserial_class)) {
		err = PTR_ERR(echoserial_class);
		printk(KERN_ERR "%s: class_create failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_class_create;
	}

	for (i = 0; i < echoserial_nports; i++) {
		err = echoserial_port_setup(&echoserial_ports[i], i);
		if (err) {
			printk(KERN_ERR "%s: echoserial_port_setup failed. i = %d, "
					"err = %d\n", DRIVER_NAME, i, err);
			goto fail_echoserial_port_setup_loop;
		}
	}

	printk(KERN_INFO "%s: initializated successfully\n", DRIVER_NAME);
	return 0;

fail_echoserial_port_setup_loop:
	while (i--)
		echoserial_port_cleanup(&echoserial_ports[i]);
	class_destroy(echoserial_class);
fail_class_create:
	uart_unregister_driver(&echoserial_driver);
fail_uart_register_driver:
	vfree(echoserial_ports);
fail_vmalloc_echoserial_ports:
	return err;
}
module_init(echoserial_init);

static void __exit echoserial_exit(void)
{
	int i;
	for (i = 0; i < echoserial_nports; i++)
		echoserial_port_cleanup(&echoserial_ports[i]);
	class_destroy(echoserial_class);
	uart_unregister_driver(&echoserial_driver);
	vfree(echoserial_ports);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(echoserial_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virt serial ports that echo back what's written to them");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

