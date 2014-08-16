#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/kfifo.h>

#define ECHOSERIAL_PORT_NAME_LEN 8
#define ECHOSERIAL_DEFAULT_BAUD 9600
/* one more than the last type @ include/uapi/linux/serial_core.h */
#define PORT_ECHOSERIAL 109

static const char DRIVER_NAME[] = "echoserial";

typedef STRUCT_KFIFO_PTR(char) echoserial_fifo_t;

struct echoserial_port {
	struct uart_port port;
	char name[ECHOSERIAL_PORT_NAME_LEN];
	/* all mutable fields in this struct are protected by port.lock */
	echoserial_fifo_t fifo;
	struct timer_list rx_timer;
	bool rx_active;
	struct timer_list tx_timer;
	bool tx_active;
	/* set during startup/set_termios */
	unsigned int baud;
	bool rtscts;
	/* fake uart machine registers */
	unsigned char mcr; /* Out: Modem Control Register */
	unsigned char lsr; /* In:  Line Status Register */
	unsigned char msr; /* In:  Modem Status Register */
};

#define uart_port_to_echoserial_port(port) \
		container_of((port), struct echoserial_port, port)

static int echoserial_nports = 1;
module_param_named(nports, echoserial_nports, int, 0444);
MODULE_PARM_DESC(nports, "number of ports to create");

static int echoserial_bsize = UART_XMIT_SIZE;
module_param_named(bsize, echoserial_bsize, int, 0444);
MODULE_PARM_DESC(bsize, "size (in bytes) for each port's fifo");

/* read/write data every X msecs. size of data written depends on baudrate */
static int echoserial_interval = 80;
module_param_named(interval, echoserial_interval, int, 0444);
MODULE_PARM_DESC(interval, "interval (in msecs) for rx/tx timers");

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
	if (echoserial_interval <= 0) {
		printk(KERN_ERR "%s: echoserial_interval <= 0. value = %d\n",
				DRIVER_NAME, echoserial_interval);
		err = -EINVAL;
	}
	/* echoserial_interval must be a multiple of 20 */
	if (echoserial_interval % 20) {
		printk(KERN_ERR "%s: echoserial_interval is not a multiple of 20. "
				"value = %d\n", DRIVER_NAME, echoserial_interval);
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

static unsigned int echoserial_tx_empty(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	unsigned int ret;
	unsigned long flags;
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	spin_lock_irqsave(&port->lock, flags);
	ret = kfifo_is_empty(&esp->fifo) ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

/* called with port->lock held */
static void echoserial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	unsigned char new_mcr, old_mcr;
	printk(KERN_INFO "%s %s: %s, mctrl=0x%x\n", DRIVER_NAME, esp->name,
			__func__, mctrl);
	new_mcr = 0;
	if (mctrl & TIOCM_RTS)
		new_mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		new_mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		new_mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		new_mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		new_mcr |= UART_MCR_LOOP;
	old_mcr = esp->mcr;
	old_mcr &= ~(UART_MCR_LOOP | UART_MCR_OUT2 | UART_MCR_OUT1 |
			UART_MCR_DTR | UART_MCR_RTS);
	esp->mcr = new_mcr | old_mcr;
}

/* called with port->lock held */
static unsigned int echoserial_get_mctrl(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	unsigned int mctrl;
	unsigned char msr;
	msr = esp->msr;
	mctrl = 0;
	if (msr & UART_MSR_DCD)
		mctrl |= TIOCM_CAR;
	if (msr & UART_MSR_RI)
		mctrl |= TIOCM_RNG;
	if (msr & UART_MSR_DSR)
		mctrl |= TIOCM_DSR;
	if (msr & UART_MSR_CTS)
		mctrl |= TIOCM_CTS;
	printk(KERN_INFO "%s %s: %s, mctrl=0x%x\n", DRIVER_NAME, esp->name,
			__func__, mctrl);
	return mctrl;
}

/* called with esp->port->lock held */
static void echoserial_do_stop_tx(struct echoserial_port *esp)
{
	esp->tx_active = false;
	del_timer(&esp->tx_timer);
}

/* called with port->lock held */
static void echoserial_stop_tx(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	echoserial_do_stop_tx(esp);
}

#define echoserial_interval_jiffies \
		(msecs_to_jiffies(echoserial_interval))

/* called with port->lock held */
static void echoserial_start_tx(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	esp->tx_active = true;
	mod_timer(&esp->tx_timer, jiffies + echoserial_interval_jiffies);
}

/* called with esp->port->lock held */
static void echoserial_do_stop_rx(struct echoserial_port *esp)
{
	esp->rx_active = false;
	del_timer(&esp->rx_timer);
}

/* called with port->lock held */
static void echoserial_stop_rx(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	echoserial_do_stop_rx(esp);
}

/* called with port->lock held */
static void echoserial_enable_ms(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
}

static int echoserial_startup(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	unsigned long flags;
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	spin_lock_irqsave(&port->lock, flags);
	esp->baud = ECHOSERIAL_DEFAULT_BAUD;
	esp->msr |= UART_MSR_CTS;
	esp->rx_active = true;
	mod_timer(&esp->rx_timer,
			jiffies + echoserial_interval_jiffies);
	spin_unlock_irqrestore(&port->lock, flags);
	return 0;
}

static void echoserial_shutdown(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	unsigned long flags;
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	spin_lock_irqsave(&port->lock, flags);
	echoserial_do_stop_rx(esp);
	echoserial_do_stop_tx(esp);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void echoserial_set_termios(struct uart_port *port,
		struct ktermios *termios, struct ktermios *old)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	tcflag_t cflag = termios->c_cflag;
	unsigned int bits, baud;
	char parity;
	const char *flow;
	unsigned long flags;
	switch (cflag & CSIZE) {
		case CS5:
			bits = 5;
			break;
		case CS6:
			bits = 6;
			break;
		case CS7:
			bits = 7;
			break;
		default: /* case CS8: */
			bits = 8;
			break;
	}
	if (cflag & PARENB) {
		if (cflag & PARODD)
			parity = 'o';
		else parity = 'e';
	} else parity = 'n';
	flow = (cflag & CRTSCTS) ? "r" : "";
	baud = uart_get_baud_rate(port, termios, old, 0, UINT_MAX);
	printk(KERN_INFO "%s %s: %s, %d%c%d%s\n", DRIVER_NAME, esp->name,
			__func__, baud, parity, bits, flow); /* example: 115200n8r */
	spin_lock_irqsave(&port->lock, flags);
	esp->baud = baud;
	esp->rtscts = !!flow[0]; /* nonempty flow string makes rtscts = 1 */
	if (esp->rtscts)
		uart_handle_cts_change(port, esp->msr & UART_MSR_CTS);
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *echoserial_type(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	return echoserial_driver.driver_name;
}

static void echoserial_release_port(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
}

static int echoserial_request_port(struct uart_port *port)
{
	struct echoserial_port *esp = uart_port_to_echoserial_port(port);
	printk(KERN_INFO "%s %s: %s\n", DRIVER_NAME, esp->name, __func__);
	return 0;
}

static struct uart_ops echoserial_uart_ops = {
	.tx_empty = echoserial_tx_empty,
	.set_mctrl = echoserial_set_mctrl,
	.get_mctrl = echoserial_get_mctrl,
	.stop_tx = echoserial_stop_tx,
	.start_tx = echoserial_start_tx,
	.stop_rx = echoserial_stop_rx,
	.enable_ms = echoserial_enable_ms,
	.startup = echoserial_startup,
	.shutdown = echoserial_shutdown,
	.set_termios = echoserial_set_termios,
	.type = echoserial_type,
	.release_port = echoserial_release_port,
	.request_port = echoserial_request_port,
};

static inline size_t echoserial_baud_to_bufsize(unsigned int baud)
{
	/* 
	 * assume all bits are data bits. ignore parity, start/stop, etc.
	 * calculation goes as follows:
	 * bitrate = bits per millisecond
	 * baud = bitrate*1000
	 * bitbuf = bitrate*buft
	 * bitbuf = baud * (buft/1000)
	 * bytebuf = bitbuf / 8 = baud * buft / 1000 / 8
	 */
	return (baud / 8) * echoserial_interval / 1000;
}

static void echoserial_rx_timer_func(unsigned long data)
{
	struct echoserial_port *esp = (struct echoserial_port *)data;
	struct uart_port *port = &esp->port;
	struct circ_buf *xmit = &port->state->xmit;
	echoserial_fifo_t *fifo = &esp->fifo;
	size_t count;
	unsigned char lsr;
	unsigned char ch;
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);
	/* rts is not set, port doesn't want to receive data */
	if (!(esp->mcr & UART_MCR_RTS) && esp->rtscts)
		goto out;
	count = min3(
		uart_circ_chars_free(xmit),
		(size_t)kfifo_len(fifo),
		echoserial_baud_to_bufsize(esp->baud)
	);
	if (count)
		printk(KERN_INFO "%s %s: %s, count=%lu\n", DRIVER_NAME, esp->name,
				__func__, count);
	lsr = esp->lsr;
	while (count--) {
		kfifo_out(fifo, &ch, 1);
		uart_insert_char(port, lsr, UART_LSR_OE, ch, TTY_NORMAL);
	}
	if (count) { /* receiving side should allow cts */
		if (!(esp->msr & UART_MSR_CTS)) {
			esp->msr |= UART_MSR_CTS;
			uart_handle_cts_change(port, 1);
		}
		tty_flip_buffer_push(port->state->port.tty);
	}
out:
	if (esp->rx_active)
		mod_timer(&esp->rx_timer,
				jiffies + echoserial_interval_jiffies);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void echoserial_tx_timer_func(unsigned long data)
{
	struct echoserial_port *esp = (struct echoserial_port *)data;
	struct uart_port *port = &esp->port;
	struct circ_buf *xmit = &port->state->xmit;
	echoserial_fifo_t *fifo = &esp->fifo;
	size_t count;
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);
	count = min3(
		uart_circ_chars_pending(xmit),
		(size_t)kfifo_avail(fifo),
		echoserial_baud_to_bufsize(esp->baud)
	);
	if (count)
		printk(KERN_INFO "%s %s: %s, count=%lu\n", DRIVER_NAME, esp->name,
				__func__, count);
	while (count--) {
		kfifo_in(fifo, &xmit->buf[xmit->tail], 1);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
	/* receiving side should disallow cts */
	if (kfifo_is_full(fifo) && esp->msr & UART_MSR_CTS) {
		esp->msr &= ~UART_MSR_CTS;
		uart_handle_cts_change(port, 0);
	}
	else if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (esp->tx_active)
		mod_timer(&esp->tx_timer,
				jiffies + echoserial_interval_jiffies);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int __init echoserial_port_setup(struct echoserial_port *esp, int i)
{
	struct uart_port *port = &esp->port;
	int err;

	snprintf(esp->name, sizeof(esp->name), "%s%d",
			echoserial_driver.dev_name, i);
	init_timer(&esp->rx_timer);
	esp->rx_timer.function = echoserial_rx_timer_func;
	esp->rx_timer.data = (unsigned long)esp;
	init_timer(&esp->tx_timer);
	esp->tx_timer.function = echoserial_tx_timer_func;
	esp->tx_timer.data = (unsigned long)esp;

	err = kfifo_alloc(&esp->fifo, echoserial_bsize, GFP_KERNEL);
	if (err) {
		printk(KERN_ERR "%s: kfifo_alloc failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_kfifo_alloc;
	}

	port->ops = &echoserial_uart_ops;
	port->line = i;
	port->type = PORT_ECHOSERIAL;
	err = uart_add_one_port(&echoserial_driver, port);
	if (err) {
		printk(KERN_ERR "%s: uart_add_one_port failed i=%d err=%d\n",
				DRIVER_NAME, i, err);
		goto fail_uart_add_one_port;
	}

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
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

