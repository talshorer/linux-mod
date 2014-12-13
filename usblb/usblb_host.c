#define MODULE_NAME "usblb_host"
#define pr_fmt(fmt) MODULE_NAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>

#include "usblb.h"

#define to_usblb_host(hcd) (*(struct usblb_host **)(hcd)->hcd_priv)

static struct class *usblb_host_class;

int usblb_host_init(void)
{
	usblb_host_class = class_create(THIS_MODULE, MODULE_NAME);
	if (IS_ERR(usblb_host_class)) {
		int err = PTR_ERR(usblb_host_class);
		pr_err("class_create failed. err = %d\n", err);
		return err;
	}
	return 0;
}

void usblb_host_exit(void)
{
	class_destroy(usblb_host_class);
}

static int usblb_host_start(struct usb_hcd *hcd)
{
	dev_info(to_usblb_host(hcd)->dev, "<%s>\n", __func__);
	return 0;
}

static void usblb_host_stop(struct usb_hcd *hcd)
{
	dev_info(to_usblb_host(hcd)->dev, "<%s>\n", __func__);
}

static int usblb_host_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct usblb_host *host = to_usblb_host(hcd);
	int ret = 0;
	unsigned long flags;

	usblb_host_lock_irqsave(host, flags);
	/* called in_irq() via usb_hcd_poll_rh_status() */
	if (host->port1_status.wPortChange) {
		*buf = 0x02;
		ret = 1;
	}
	usblb_host_unlock_irqrestore(host, flags);
	return ret;
}

static int usblb_host_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		u16 wIndex, char *buf, u16 wLength)
{
	struct usblb_host *host = to_usblb_host(hcd);
	int ret = 0;
	unsigned long flags;

	dev_info(host->dev, "<%s> typeReq=0x%04x wValue=0x%04x wIndex=0x%04x "
			"wLength=0x%04x\n", __func__,
			typeReq, wValue, wIndex, wLength);

	usblb_host_lock_irqsave(host, flags);
	switch (typeReq) {
	case SetHubFeature:
		/* fallthrough */
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case GetHubStatus:
		memset(buf, 0, sizeof(__le32));
		/* *(__le32 *)buf = cpu_to_le32(0); */
		break;
	case GetHubDescriptor:
		{
		/* based on drivers/usb/musb/musb_virthub.c */
		struct usb_hub_descriptor *desc = (void *)buf;
		desc->bDescLength = 9;
		desc->bDescriptorType = 0x29;
		desc->bNbrPorts = 1;
		desc->wHubCharacteristics = cpu_to_le16(
			  0x0001 /* per-port power switching */
			| 0x0010 /* no overcurrent reporting */
		);
		desc->bPwrOn2PwrGood = 5; /* msec/2 */
		desc->bHubContrCurrent = 0;
		desc->u.hs.DeviceRemovable[0] = 0x02; /* port 1 */
		desc->u.hs.DeviceRemovable[1] = 0xff;
		}
		break;
	case ClearPortFeature:
		if ((wIndex & 0xff) != 1)
			goto error;
		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			host->port1_status.wPortStatus &= ~USB_PORT_STAT_POWER;
			usblb_host_spawn_event(host, USBLB_E_DISC);
			break;
		case USB_PORT_FEAT_ENABLE:
			host->port1_status.wPortStatus &=
					~USB_PORT_STAT_ENABLE;
			break;
		default:
			goto error;
		}
		break;
	case GetPortStatus:
		{
		struct usb_port_status *port_status = (void *)buf;
		if ((wIndex & 0xff) != 1)
			goto error;
		memcpy(port_status, &host->port1_status, sizeof(port_status));
		}
		break;
	case SetPortFeature:
		if ((wIndex & 0xff) != 1)
			goto error;
		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			host->port1_status.wPortStatus |= USB_PORT_STAT_POWER;
			usblb_host_spawn_event(host, USBLB_E_CONN);
			break;
		case USB_PORT_FEAT_ENABLE:
			host->port1_status.wPortStatus |= USB_PORT_STAT_ENABLE;
			break;
		default:
			goto error;
		}
		break;
	default:
error:
		/* "stall" on error */
		ret = -EPIPE;
	}
	usblb_host_unlock_irqrestore(host, flags);
	return ret;
}

static const struct hc_driver usblb_host_driver = {
	.description            = KBUILD_MODNAME "-hcd",
	.product_desc           = KBUILD_MODNAME " host driver",
	.hcd_priv_size          = sizeof(struct usblb_host *),
	.flags                  = HCD_USB2 | HCD_MEMORY,

	.start                  = usblb_host_start,
	.stop                   = usblb_host_stop,

#if 0 /* TODO */
	.get_frame_number       = usblb_host_get_frame_number,

	.urb_enqueue            = usblb_host_urb_enqueue,
	.urb_dequeue            = usblb_host_urb_dequeue,
	.endpoint_disable       = usblb_host_disable,
#endif /* 0 */

	.hub_status_data        = usblb_host_hub_status_data,
	.hub_control            = usblb_host_hub_control,
#if 0 /* TODO */
	.bus_suspend            = usblb_host_bus_suspend,
	.bus_resume             = usblb_host_bus_resume,
	/* .start_port_reset    = NULL, */
	/* .hub_irq_enable      = NULL, */
#endif /* 0 */
};

int usblb_host_device_setup(struct usblb_host *host, int i)
{
	int err;

	host->dev = device_create(usblb_host_class, NULL, MKDEV(0, i), host,
			"%s%d", usblb_host_class->name, i);
	if (IS_ERR(host->dev)) {
		err = PTR_ERR(host->dev);
		pr_err("device_create failed. i = %d, err = %d\n", i, err);
		goto fail_device_create;
	}

	host->hcd = usb_create_hcd(&usblb_host_driver,
			host->dev, dev_name(host->dev));
	if (!host->hcd) {
		err = -ENOMEM;
		pr_err("usb_create_hcd failed for %s\n", dev_name(host->dev));
		goto fail_usb_create_hcd;
	}
	*host->hcd->hcd_priv = (unsigned long)host;

	err = usb_add_hcd(host->hcd, 0, 0);
	if (err) {
		pr_err("usb_add_hcd failed for %s\n", dev_name(host->dev));
		goto fail_usb_add_hcd;
	}

	pr_info("created %s successfully\n", dev_name(host->dev));
	return 0;

fail_usb_add_hcd:
	usb_put_hcd(host->hcd);
fail_usb_create_hcd:
	device_destroy(usblb_host_class, host->dev->devt);
fail_device_create:
	return err;
}

void usblb_host_device_cleanup(struct usblb_host *host)
{
	pr_info("destroying %s\n", dev_name(host->dev));
	usb_remove_hcd(host->hcd);
	usb_put_hcd(host->hcd);
	device_destroy(usblb_host_class, host->dev->devt);
}

int usblb_host_set_gadget(struct usblb_host *h, struct usblb_gadget *g)
{
	int err;

	err = sysfs_create_link(&h->dev->kobj, &g->dev->kobj, "gadget");
	if (err) {
		pr_info("sysfs_create_link failed. i = %d, err = %d\n",
			MINOR(g->dev->devt), err);
		goto fail_sysfs_create_link;
	}

	return 0;

fail_sysfs_create_link:
	return err;
}
