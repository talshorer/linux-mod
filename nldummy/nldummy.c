#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/sock.h>

#include "nldummy_uapi.h"

#ifndef CONFIG_NET
#error CONFIG_NET is not set
#endif

struct nldummy_sock {
	struct list_head list;
	struct sock *sk;
};

static LIST_HEAD(nldummy_sock_list);
static DEFINE_MUTEX(nldummy_sock_mutex); /* protects nldummy_sock_list */

static int nldummy_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	size_t len = nlh->nlmsg_len - NLMSG_HDRLEN;
	struct sk_buff *skb_out;
	const struct nlmsghdr *nlh_out;
	int i;
	int err;

	pr_info("received message of len %u from pid %d\n",
			nlh->nlmsg_len, nlh->nlmsg_pid);
	print_hex_dump(KERN_INFO, pr_fmt("\trx: "), DUMP_PREFIX_OFFSET, 16, 1,
			nlmsg_data(nlh), len, false);

	skb_out = nlmsg_new(len, GFP_KERNEL);
	if (!skb_out) {
		pr_err("<%s> failed to allocated socket buffer\n", __func__);
		return -ENOMEM;
	}
	nlh_out = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, len, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
	memcpy(nlmsg_data(nlh_out), nlmsg_data(nlh), len);
	for (i = 0; i < len; i++)
		((char *)nlmsg_data(nlh_out))[i] ^= 0xff;
	print_hex_dump(KERN_INFO, pr_fmt("\ttx: "), DUMP_PREFIX_OFFSET, 16, 1,
			nlmsg_data(nlh_out), len, false);
	err = nlmsg_unicast(skb->sk, skb_out, nlh->nlmsg_pid);
	if (err)
		pr_err("<%s> failed to send response to userspace, err = %d",
				__func__, err);
	return err;
}

static void nldummy_input(struct sk_buff *skb)
{
	netlink_rcv_skb(skb, &nldummy_rcv_msg);
}

static void nldummy_bind(int group)
{
	pr_info("<%s> group=%d\n", __func__, group);
}

static int nldummy_net_init(struct net *net)
{
	struct nldummy_sock *nd_sk;
	struct netlink_kernel_cfg cfg = {
		.groups = 1,
		.input = nldummy_input,
		.bind = nldummy_bind,
	};

	pr_info("<%s> net=%p\n", __func__, net);

	nd_sk = kzalloc(sizeof(*nd_sk), GFP_KERNEL);
	if (!nd_sk) {
		pr_err("<%s> failed to allocate nd_sk\n", __func__);
		return -ENOMEM;
	}

	nd_sk->sk = netlink_kernel_create(net, NETLINK_DUMMY, &cfg);
	if (!nd_sk->sk) {
		pr_err("<%s> failed to create netlink socket\n", __func__);
		kfree(nd_sk);
		return -ENODEV;
	}

	mutex_lock(&nldummy_sock_mutex);
	list_add(&nd_sk->list, &nldummy_sock_list);
	mutex_unlock(&nldummy_sock_mutex);

	return 0;
}

static void nldummy_net_exit(struct net *net)
{
	struct nldummy_sock *nd_sk;

	pr_info("<%s> net=%p\n", __func__, net);

	mutex_lock(&nldummy_sock_mutex);
	list_for_each_entry(nd_sk, &nldummy_sock_list, list)
		if (sock_net(nd_sk->sk) == net)
			goto found;
	mutex_unlock(&nldummy_sock_mutex);
	pr_warn("<%s> failed to find nldummy socket for net %p\n",
			__func__, net);
	return;
found:
	list_del(&nd_sk->list);
	mutex_unlock(&nldummy_sock_mutex);

	netlink_kernel_release(nd_sk->sk);
	kfree(nd_sk);
}

static struct pernet_operations nldummy_net_ops = {
	.init = nldummy_net_init,
	.exit = nldummy_net_exit,
};

module_driver(nldummy_net_ops, register_pernet_subsys,
		unregister_pernet_subsys);


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A netlink server that xors incoming packets");
MODULE_VERSION("1.0.2");
MODULE_LICENSE("GPL");
