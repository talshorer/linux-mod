/* TODO
 * create a class for chrdev pipes
 * add chrdev to virtnet_iface
 * make tx write to the chrdev's fifo (add the fifo to struct virtnet_iface)
 * figure out how to destroy class from last virtnet_free_netdev
 * write chrdev ops (based on cpipe)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/u64_stats_sync.h>
#include <net/rtnetlink.h>

static const char DRIVER_NAME[] = "virtnet";

struct virtnet_iface {
};

struct pcpu_dstats {
	u64 tx_packets;
	u64 tx_bytes;
	struct u64_stats_sync syncp;
};

static int virtnet_nifaces = 1;
module_param_named(nifaces, virtnet_nifaces, int, 0444);
MODULE_PARM_DESC(nifaces, "number of ifaces to create");

static int __init virtnet_check_module_params(void) {
	int err = 0;
	if (virtnet_nifaces < 0) {
		printk(KERN_ERR "%s: virtnet_nifaces < 0. value = %d\n",
				DRIVER_NAME, virtnet_nifaces);
		err = -EINVAL;
	}
	return err;
}

static const char virtnet_iface_fmt[] = "virt%d";

static int virtnet_dev_init(struct net_device *dev)
{
	dev->dstats = alloc_percpu(struct pcpu_dstats);
	if (!dev->dstats)
		return -ENOMEM;

	return 0;
}

static void virtnet_dev_uninit(struct net_device *dev)
{
	free_percpu(dev->dstats);
}

/* fake multicast ability */
static void virtnet_set_multicast_list(struct net_device *dev)
{
}

static struct rtnl_link_stats64 *virtnet_get_stats64(struct net_device *dev,
		struct rtnl_link_stats64 *stats)
{
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_dstats *dstats;
		u64 tbytes, tpackets;
		unsigned int start;

		dstats = per_cpu_ptr(dev->dstats, i);
		do {
			start = u64_stats_fetch_begin_bh(&dstats->syncp);
			tbytes = dstats->tx_bytes;
			tpackets = dstats->tx_packets;
		} while (u64_stats_fetch_retry_bh(&dstats->syncp, start));
		stats->tx_bytes += tbytes;
		stats->tx_packets += tpackets;
	}
	return stats;
}

static netdev_tx_t virtnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	dstats->tx_packets++;
	dstats->tx_bytes += skb->len;
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
	printk(KERN_INFO "%s: destroying interface %s\n", DRIVER_NAME, dev->name);
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
	.priv_size = sizeof(struct virtnet_iface),
	.setup = virtnet_setup,
	.validate = virtnet_validate,
};

int virtnet_init_iface(void)
{
	struct net_device *dev;
	int err;

	dev = alloc_netdev(sizeof(struct virtnet_iface),
			virtnet_iface_fmt, virtnet_setup);
	if (!dev) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: alloc_netdev failed\n", DRIVER_NAME);
		goto fail_alloc_netdev;
		
	}

	dev->rtnl_link_ops = &virtnet_link_ops;
	err = register_netdevice(dev);
	if (err) {
		printk(KERN_ERR "%s: register_netdevice failed\n", DRIVER_NAME);
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
			printk(KERN_ERR "%s: virtnet_init_device failed. i = %d, "
					"err = %d\n", DRIVER_NAME, i, err);
			goto fail_virtnet_init_device_loop;
		}
	}
	rtnl_unlock();

	printk(KERN_INFO "%s: initializated successfully\n", DRIVER_NAME);
	return 0;

fail_virtnet_init_device_loop:
	rtnl_unlock();
	rtnl_link_unregister(&virtnet_link_ops);
fail_rtnl_link_register:
	return err;
}
module_init(virtnet_init);

static void __exit virtnet_exit(void)
{
	rtnl_link_unregister(&virtnet_link_ops);
}
module_exit(virtnet_exit);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("Virtual net interfaces that pipe to char devices");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
