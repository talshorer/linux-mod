#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>

#define IFACENAME_BASE "virt"

/* arbitrary value >= 0x600 */
#define VIRTNET_ETHTYPE 0x700

static char *prog;
static char dummy_macs[] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* dst */
	0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, /* src */
};
static char dummy_data[] = "Hello, world!";

#define TOTAL_PACKET_SIZE (ETH_HLEN + sizeof(dummy_data) - 1)

static void populate_packet(char *packet)
{
	memcpy(packet, dummy_macs, sizeof(dummy_macs));
	*(uint16_t *)&packet[sizeof(dummy_macs)] = htons(VIRTNET_ETHTYPE);
	memcpy(&packet[ETH_HLEN], dummy_data, TOTAL_PACKET_SIZE - ETH_HLEN);
}

#define LB_DELAY_MS 1000 /* one second */
#define LB_TOLERANCE_MS (LB_DELAY_MS / 100) /* 1%. arbitrary */

static int test_lb(int sfd, unsigned int iface_id)
{
	char packet[TOTAL_PACKET_SIZE];
	char readback[TOTAL_PACKET_SIZE];
	struct timeval t_start, t_end;
	unsigned long actual_time;
	populate_packet(packet);
	gettimeofday(&t_start, NULL);
	write(sfd, packet, TOTAL_PACKET_SIZE);
	read(sfd, readback, TOTAL_PACKET_SIZE);
	gettimeofday(&t_end, NULL);
	if (memcmp(packet, readback, TOTAL_PACKET_SIZE)) {
		dprintf(2, "%s: failed loopback test\n", prog);
		return 1;
	}
	actual_time = (1000 * (t_end.tv_sec - t_start.tv_sec) +
			(t_end.tv_usec - t_start.tv_usec) / 1000);
	if (actual_time > LB_DELAY_MS + LB_TOLERANCE_MS ||
			actual_time < LB_DELAY_MS - LB_TOLERANCE_MS) {
		dprintf(2, "%s: failed timing test\n", prog);
		return 1;
	}
	return 0;
}

#define CHRDEV_BASE "/dev/virtnet_chr"
#define CHRDEV_NAME_LEN (sizeof(CHRDEV_BASE) + 4)

static int test_chr(int sfd, unsigned int iface_id)
{
	int cfd;
	int ret;
	char chrdev_name[CHRDEV_NAME_LEN];
	char packet[TOTAL_PACKET_SIZE];
	char readback[TOTAL_PACKET_SIZE];
	populate_packet(packet);
	sprintf(chrdev_name, CHRDEV_BASE "%u", iface_id);
	cfd = open(chrdev_name, O_RDWR | O_NONBLOCK);
	if (cfd < 0) {
		perror("Failed to open chrdev");
		return 1;
	}
	write(sfd, packet, TOTAL_PACKET_SIZE);
	/* drain all packets. buffer should have last packet (one we sent) */
	while (read(cfd, readback, TOTAL_PACKET_SIZE) >= 0);
	write(cfd, readback, TOTAL_PACKET_SIZE);
	memset(readback, 0, TOTAL_PACKET_SIZE);
	read(sfd, readback, TOTAL_PACKET_SIZE);
	if (memcmp(packet, readback, TOTAL_PACKET_SIZE)) {
		dprintf(2, "%s: failed readback test\n", prog);
		ret = 1;
		goto close_chrdev;
	}
	ret = 0;
close_chrdev:
	close(cfd);
	return ret;
}

typedef int (*virtnet_test_fn)(int, unsigned int);

struct backend_test {
	virtnet_test_fn test_fn;
	char *name;
};

#define test_entry(_name)    \
{	                         \
	.test_fn = test_##_name, \
	.name = #_name           \
}

static struct backend_test all_tests[] = {
	test_entry(lb),
	test_entry(chr),
};

int main(int argc, char *argv[])
{
	int i;
	int sfd;
	int ret;
	unsigned int iface_id;
	char iface_name[IFNAMSIZ];
	const char *backend;
	struct sockaddr_ll sock_address;
	virtnet_test_fn test_fn = NULL;
	prog = argv[0];
	if (argc != 3)
		goto usage;
	backend = argv[1];
	iface_id = (unsigned int)strtol(argv[2], NULL, 10);
	sprintf(iface_name, IFACENAME_BASE "%u", iface_id);
	for (i = 0; i < sizeof(all_tests) / sizeof(all_tests[0]); i++)
		if (!strcmp(backend, all_tests[i].name)) {
			test_fn = all_tests[i].test_fn;
			break;
		}
	if (!test_fn) {
		dprintf(2, "%s: unknown backend %s\n", prog, backend);
		return 1;
	}
	sfd = socket(PF_PACKET, SOCK_RAW, htons(VIRTNET_ETHTYPE));
	if (sfd < 0) {
		perror("Failed to create socket");
		return 1;
	}
	memset(&sock_address, 0, sizeof(sock_address));
	sock_address.sll_family = PF_PACKET;
	sock_address.sll_protocol = htons(VIRTNET_ETHTYPE);
	sock_address.sll_ifindex = if_nametoindex(iface_name);
	if (bind(sfd, (struct sockaddr *)&sock_address,
			sizeof(sock_address)) < 0) {
		perror("Failed to bind socket");
		ret = 1;
		goto close_sock;
	}
	ret = test_fn(sfd, iface_id);
close_sock:
	close(sfd);
	return ret;
usage:
	dprintf(2, "usage: %s BACKEND IFACE_ID\n", prog);
	printf("%d\n", argc);
	return 1;
}
