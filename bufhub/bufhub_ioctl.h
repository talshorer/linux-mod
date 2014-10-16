#ifndef _BUFHUB_IOCTL_H
#define _BUFHUB_IOCTL_H

#include <linux/ioctl.h>

#define BUFHUB_IOC_MAGIC 'b'
#define BUFHUB_IOCCREATE  _IOR(BUFHUB_IOC_MAGIC, 0, unsigned int)
#define BUFHUB_IOCDESTROY _IOW(BUFHUB_IOC_MAGIC, 1, unsigned int)
#define BUFHUB_IOC_MAXNR 1

#endif /* _BUFHUB_IOCTL_H */
