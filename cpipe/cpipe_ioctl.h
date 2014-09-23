#ifndef _CPIPE_IOCTL_H
#define _CPIPE_IOCTL_H

#include <linux/ioctl.h>

#define CPIPE_IOC_MAGIC 'p'
#define CPIPE_IOCGAVAILRD _IOR(CPIPE_IOC_MAGIC, 0, int)
#define CPIPE_IOCGAVAILWR _IOR(CPIPE_IOC_MAGIC, 1, int)
#define CPIPE_IOC_MAXNR 1

#endif /* _CPIPE_IOCTL_H */

