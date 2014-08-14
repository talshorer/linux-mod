#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include <linux/kfifo.h>

#define ECHOSERIAL_PORT_NAME_LEN 8

static const char DRIVER_NAME[] = "echoserial";

typedef STRUCT_KFIFO_PTR(char) echoserial_fifo_t;

struct echoserial_port {
	struct uart_port port;
	echoserial_fifo_t fifo;
	char name[ECHOSERIAL_PORT_NAME_LEN];
};

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
	/* .nr is set during init */
};

/* TODO port ops */

static struct uart_ops echoserial_uart_ops = {

};

static int __init echoserial_port_setup(struct echoserial_port *esp, int i)
{
	struct uart_port *uport = &esp->port;
	int err;

	err = kfifo_alloc(&esp->fifo, echoserial_bsize, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "%s: kfifo_alloc failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_kfifo_alloc;
	}

	uport->ops = &echoserial_uart_ops;
	uport->line = i;
	err = uart_add_one_port(&echoserial_driver, uport);
	if (err) {
		printk(KERN_ERR "%s: uart_add_one_port failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_uart_add_one_port;
	}

	snprintf(esp->name, sizeof(esp->name), "%s%d",
			echoserial_driver.dev_name, i);

	printk(KERN_INFO "%s: created port %s successfully\n",
			DRIVER_NAME, esp->name);

	return 0;
fail_uart_add_one_port:
	kfifo_free(&esp->fifo);
fail_kfifo_alloc:
	return err;
}

static void echoserial_port_cleanup(struct echoserial_port *esp)
{
	printk(KERN_INFO "%s: destroying port %s\n", DRIVER_NAME, esp->name);
	uart_remove_one_port(&echoserial_driver, &esp->port);
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
	memset(echoserial_ports, 0,
			sizeof(echoserial_ports[0]) * echoserial_nports);

	echoserial_driver.nr = echoserial_nports;
	err = uart_register_driver(&echoserial_driver);
	if (err) {
		printk(KERN_ERR "%s: uart_register_driver failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_uart_register_driver;
	}
	echoserial_driver.major = echoserial_driver.tty_driver->major;
	echoserial_driver.minor = echoserial_driver.tty_driver->minor_start;

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
	uart_unregister_driver(&echoserial_driver);
	vfree(echoserial_ports);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(echoserial_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virt serial ports that echo back what's written to them");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

