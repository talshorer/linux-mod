#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/u64_stats_sync.h>
#include <net/rtnetlink.h>

#include "virtnet.h"

static const char DRIVER_NAME[] = "virtnet";

struct pcpu_dstats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_dropped;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_dropped;
	struct u64_stats_sync syncp;
};

static int virtnet_nifaces = 1;
module_param_named(nifaces, virtnet_nifaces, int, 0444);
MODULE_PARM_DESC(nifaces, "number of ifaces to create");

static bool virtnet_packetdump = false;
module_param_named(packetdump, virtnet_packetdump, bool, 0644);
MODULE_PARM_DESC(packetdump, "print incoming and outgoing packets to log");

static char *virtnet_backend = "lb";
module_param_named(backend, virtnet_backend, charp, 0444);
MODULE_PARM_DESC(backend, "backend to use");

static struct virtnet_backend_ops *virtnet_backend_ops;

static int __init virtnet_check_module_params(void) {
	int err = 0;
	if (virtnet_nifaces < 0) {
		printk(KERN_ERR "%s: virtnet_nifaces < 0. value = %d\n",
				DRIVER_NAME, virtnet_nifaces);
		err = -EINVAL;
	}
	virtnet_backend_ops = virtnet_get_backend(virtnet_backend);
	if (!virtnet_backend_ops)
		err = -EINVAL;
	return err;
}

static inline int virtnet_backend_init(unsigned int nifaces)
{
	if (virtnet_backend_ops->init)
		return virtnet_backend_ops->init(nifaces);
	return 0;
}

static inline void virtnet_backend_exit(void)
{
	if (virtnet_backend_ops->exit)
		virtnet_backend_ops->exit();
}

static inline int virtnet_backend_dev_init(void *priv, unsigned int minor)
{
	if (virtnet_backend_ops->dev_init)
		return virtnet_backend_ops->dev_init(priv, minor);
	return 0;
}

static inline void virtnet_backend_dev_uninit(void *priv)
{
	if (virtnet_backend_ops->dev_uninit)
		virtnet_backend_ops->dev_uninit(priv);
}

static inline int virtnet_backend_xmit(struct net_device *dev,
		const char *buf, size_t len)
{
	if (virtnet_backend_ops->xmit)
		return virtnet_backend_ops->xmit(dev, buf, len);
	return -ENODEV;
}

#define virtnet_backend_priv_size (virtnet_backend_ops->priv_size)

static const char virtnet_iface_fmt[] = "virt%d";

static int virtnet_dev_init(struct net_device *dev)
{
	unsigned int minor;
	int err;

	printk(KERN_INFO "%s: interface %s invoked ndo <%s>\n", DRIVER_NAME,
			dev->name, __func__);

	dev->dstats = alloc_percpu(struct pcpu_dstats);
	if (!dev->dstats)
		return -ENOMEM;

	sscanf(dev->name, virtnet_iface_fmt, &minor);
	err = virtnet_backend_dev_init(netdev_priv(dev), minor);
	if (err) {
		free_percpu(dev->dstats);
		return err;
	}

	return 0;
}

static void virtnet_dev_uninit(struct net_device *dev)
{
	printk(KERN_INFO "%s: interface %s invoked ndo <%s>\n", DRIVER_NAME,
			dev->name, __func__);
	virtnet_backend_dev_uninit(netdev_priv(dev));
	free_percpu(dev->dstats);
}

/* fake multicast ability */
static void virtnet_set_multicast_list(struct net_device *dev)
{
	printk(KERN_INFO "%s: interface %s invoked ndo <%s>\n", DRIVER_NAME,
			dev->name, __func__);
}

static struct rtnl_link_stats64 *virtnet_get_stats64(struct net_device *dev,
		struct rtnl_link_stats64 *stats)
{
	int i;

	printk(KERN_INFO "%s: interface %s invoked ndo <%s>\n", DRIVER_NAME,
			dev->name, __func__);

	for_each_possible_cpu(i) {
		const struct pcpu_dstats *dstats;
		u64 tbytes, tpackets, tdropped;
		u64 rbytes, rpackets, rdropped;
		unsigned int start;

		dstats = per_cpu_ptr(dev->dstats, i);
		do {
			start = u64_stats_fetch_begin_irq(&dstats->syncp);
			tbytes = dstats->tx_bytes;
			tpackets = dstats->tx_packets;
			tdropped = dstats->tx_dropped;
			rbytes = dstats->rx_bytes;
			rpackets = dstats->rx_packets;
			rdropped = dstats->rx_dropped;
		} while (u64_stats_fetch_retry_irq(&dstats->syncp, start));
		stats->tx_bytes += tbytes;
		stats->tx_packets += tpackets;
		stats->tx_dropped += tdropped;
		stats->rx_bytes += rbytes;
		stats->rx_packets += rpackets;
		stats->rx_dropped += rdropped;
	}
	return stats;
}

static netdev_tx_t virtnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);
	int err;

	printk(KERN_INFO "%s: interface %s invoked ndo <%s>\n", DRIVER_NAME,
			dev->name, __func__);

	skb_orphan(skb);

	if (virtnet_packetdump) {
		printk(KERN_INFO "%s: interface %s tx packet of length %d\n",
				DRIVER_NAME, dev->name, skb->len);
		print_hex_dump(KERN_INFO, "tx data: ", DUMP_PREFIX_OFFSET, 16,
				1, skb->data, skb->len, false);
	}

	err = virtnet_backend_xmit(dev, skb->data, skb->len);
	u64_stats_update_begin(&dstats->syncp);
	if (err) {
		dev->stats.tx_errors++;
		dev->stats.tx_dropped++;
	} else {
		dstats->tx_packets++;
		dstats->tx_bytes += skb->len;
	}
	u64_stats_update_end(&dstats->syncp);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops virtnet_netdev_ops = {
	.ndo_init		= virtnet_dev_init,
	.ndo_uninit		= virtnet_dev_uninit,
	.ndo_start_xmit		= virtnet_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= virtnet_set_multicast_list,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= virtnet_get_stats64,
};

static void virtnet_free_netdev(struct net_device *dev)
{
	printk(KERN_INFO "%s: destroying interface %s\n",
			DRIVER_NAME, dev->name);
	free_netdev(dev);
}

static void virtnet_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &virtnet_netdev_ops;
	dev->destructor = virtnet_free_netdev;
	dev->features = NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_TSO |
			NETIF_F_HW_CSUM;
	eth_hw_addr_random(dev);
}

static int virtnet_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static struct rtnl_link_ops virtnet_link_ops = {
	.kind = DRIVER_NAME,
	.priv_size = 0,
	.setup = virtnet_setup,
	.validate = virtnet_validate,
};

/* simulate an rx packet transport */
int virtnet_recv(struct net_device *dev, const char *buf, size_t len)
{
	struct sk_buff *skb;
	struct pcpu_dstats *dstats;
	char *dst;
	int err = 0;

	skb = netdev_alloc_skb(dev, len);
	if (unlikely(!skb)) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: <%s> netdev_alloc_skb failed\n",
				DRIVER_NAME, __func__);
		goto fail_netdev_alloc_skb;
	}
	skb_reserve(skb, NET_IP_ALIGN);
	dst = skb_put(skb, len);
	memcpy(dst, buf, len);
	skb->protocol = eth_type_trans(skb, dev);

	switch(in_interrupt() ? netif_rx(skb) : netif_rx_ni(skb)) {
	case NET_RX_DROP:
		err = -EIO;
		printk(KERN_ERR "%s: <%s> netif_rx failed\n",
				DRIVER_NAME, __func__);
		break;
	case NET_RX_SUCCESS:
		/* HACK: eth_type_trans() pulled ETH_HLEN bytes from skb's
		 * head, so when we print the buffer, we subtract ETH_HLEN from
		 * the pointer and add ETH_HLEN to the length. This has no
		 * effect on upper layer since all it changes is the log
		 * messages.
		 */
		if (virtnet_packetdump) {
			printk(KERN_INFO "%s: interface %s rx packet "
					"of length %d\n", DRIVER_NAME,
					dev->name, skb->len + ETH_HLEN);
			print_hex_dump(KERN_INFO, "rx data: ",
					DUMP_PREFIX_OFFSET, 16, 1,
					skb->data - ETH_HLEN,
					skb->len + ETH_HLEN, false);
		}
		break;
	}

	/* don't free skb unless an error occured
	 * the higher layers will do that for us
	 */
	if (err)
		dev_kfree_skb(skb);
fail_netdev_alloc_skb:
	dstats = this_cpu_ptr(dev->dstats);
	u64_stats_update_begin(&dstats->syncp);
	if (err) {
		dev->stats.rx_errors++;
		dev->stats.rx_dropped++;
	} else {
		dstats->rx_packets++;
		dstats->rx_bytes += len;
	}
	u64_stats_update_end(&dstats->syncp);
	return err;
}

static int virtnet_init_iface(void)
{
	struct net_device *dev;
	int err;

	dev = alloc_netdev(virtnet_backend_priv_size,
			virtnet_iface_fmt, virtnet_setup);
	if (!dev) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: alloc_netdev failed\n", DRIVER_NAME);
		goto fail_alloc_netdev;

	}

	dev->rtnl_link_ops = &virtnet_link_ops;
	err = register_netdevice(dev);
	if (err) {
		printk(KERN_ERR "%s: register_netdevice failed\n",
				DRIVER_NAME);
		goto fail_register_netdevice;
	}

	printk(KERN_INFO "%s: created interface %s successfully\n",
			DRIVER_NAME, dev->name);
	return 0;

fail_register_netdevice:
	free_netdev(dev);
fail_alloc_netdev:
	return err;
}

static int __init virtnet_init(void)
{
	int err;
	int i;

	err = virtnet_check_module_params();
	if (err)
		return err;

	err = virtnet_backend_init(virtnet_nifaces);
	if (err) {
		printk(KERN_ERR "%s: virtnet_backend_init failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_virtnet_backend_init;
	}

	err = rtnl_link_register(&virtnet_link_ops);
	if (err) {
		printk(KERN_ERR "%s: rtnl_link_register failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_rtnl_link_register;
	}

	rtnl_lock();
	for (i = 0; i < virtnet_nifaces; i++) {
		err = virtnet_init_iface();
		if (err) {
			printk(KERN_ERR "%s: virtnet_init_device failed. "
					"i = %d, err = %d\n",
					DRIVER_NAME, i, err);
			goto fail_virtnet_init_device_loop;
		}
	}
	rtnl_unlock();

	printk(KERN_INFO "%s: initializated successfully, backend = %s\n",
			DRIVER_NAME, virtnet_backend);
	return 0;

fail_virtnet_init_device_loop:
	rtnl_unlock();
	rtnl_link_unregister(&virtnet_link_ops);
fail_rtnl_link_register:
	virtnet_backend_exit();
fail_virtnet_backend_init:
	return err;
}
module_init(virtnet_init);

static void __exit virtnet_exit(void)
{
	rtnl_link_unregister(&virtnet_link_ops);
	virtnet_backend_exit();
}
module_exit(virtnet_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virtual net interfaces that pipe to char devices");
MODULE_VERSION("1.2.4");
MODULE_LICENSE("GPL");
