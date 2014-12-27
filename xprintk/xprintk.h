#ifndef _XPRINTK_H
#define _XPRINTK_H

struct xprintk_x {
};

extern char *x_name(struct xprintk_x *);

#define __x_printk(level, x, fmt, ...) \
	printk(level "%s: " fmt, x_name(x), __VA_ARGS__)

#define x_debug(...) __x_printk(KERN_DEBUG, __VA_ARGS__)
#define x_info(...) __x_printk(KERN_INFO, __VA_ARGS__)
#define x_notice(...) __x_printk(KERN_NOTICE, __VA_ARGS__)
#define x_warn(...) __x_printk(KERN_WARNING, __VA_ARGS__)
#define x_err(...) __x_printk(KERN_ERR, __VA_ARGS__)
#define x_crit(...) __x_printk(KERN_CRIT, __VA_ARGS__)
#define x_alert(...) __x_printk(KERN_ALERT, __VA_ARGS__)
#define x_emerg(...) __x_printk(KERN_EMERG, __VA_ARGS__)


#endif /* _XPRINTK_H */
