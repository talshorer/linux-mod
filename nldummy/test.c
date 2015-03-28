#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "nldummy_uapi.h"

char *prog;

static struct nlmsghdr *nldummy_create_header(size_t len)
{
	struct nlmsghdr *nlh;

	nlh = malloc(NLMSG_SPACE(len));
	if (!nlh) {
		dprintf(2, "%s: failed to allocate netlink header\n", prog);
		return NULL;
	}
	memset(nlh, 0, NLMSG_SPACE(len));
	nlh->nlmsg_len = NLMSG_SPACE(len);
	nlh->nlmsg_pid = getpid();
	/* if (nlmsg_type < NLMSG_MIN_TYPE), the kernel skips the message */
	nlh->nlmsg_type = NLMSG_MIN_TYPE;
	return nlh;
}

static void prepare_message(struct sockaddr_nl *dst, struct msghdr *msg,
		struct iovec *iov)
{
	memset(dst, 0, sizeof(*dst));
	dst->nl_family = AF_NETLINK;
	dst->nl_pid = 0; /* For Linux Kernel */
	memset(iov, 0, sizeof(*iov));
	memset(msg, 0, sizeof(*msg));
	msg->msg_iov = iov;
	msg->msg_iovlen = 1;
}

static int nldummy_send(int fd, const char *buf, size_t len)
{
	int ret;
	struct sockaddr_nl dst;
	struct msghdr msg;
	struct iovec iov;
	struct nlmsghdr *nlh;

	nlh = nldummy_create_header(len);
	if (!nlh)
		goto out_none;
	nlh->nlmsg_flags |= NLM_F_REQUEST;
	memcpy(NLMSG_DATA(nlh), buf, len);

	prepare_message(&dst, &msg, &iov);
	dst.nl_groups = 0; /* unicast */
	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	msg.msg_name = (void *)&dst;
	msg.msg_namelen = sizeof(dst);

	if (sendmsg(fd, &msg, 0) < 0) {
		perror("sendmsg");
		ret = 1;
		goto out_free_nlh;
	}
	ret = 0;

out_free_nlh:
	free(nlh);
out_none:
	return ret;
}

static int nldummy_recv(int fd, char *buf, size_t len)
{
	int ret;
	struct sockaddr_nl dst;
	struct msghdr msg;
	struct iovec iov;
	struct nlmsghdr *nlh;

	nlh = nldummy_create_header(len);
	if (!nlh)
		goto out_none;

	prepare_message(&dst, &msg, &iov);
	dst.nl_groups = 0; /* unicast */
	iov.iov_base = (void *)nlh;
	iov.iov_len = nlh->nlmsg_len;
	msg.msg_name = (void *)&dst;
	msg.msg_namelen = sizeof(dst);

	if (recvmsg(fd, &msg, 0) < 0) {
		perror("recvmsg");
		ret = 1;
		goto out_free_nlh;
	}
	memcpy(buf, NLMSG_DATA(nlh), len);

	ret = 0;

out_free_nlh:
	free(nlh);
out_none:
	return ret;
}

static void printbuf(const char *prefix, const char *buf, size_t len)
{
	int i;

	dprintf(2, "%s", prefix);
	for (i = 0; i < len; i++)
		dprintf(2, "%02x ", (unsigned char)buf[i]);
	dprintf(2, "\n");
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	int i;
	struct sockaddr_nl src;
	char buf[] = "Hello";
	char expected[sizeof(buf)];
	char readback[sizeof(buf)];

	prog = argv[0];

	fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_DUMMY);
	if (fd < 0) {
		perror("socket");
		ret = 1;
		goto out_none;
	}

	memset(&src, 0, sizeof(src));
	src.nl_family = AF_NETLINK;
	src.nl_pid = getpid(); /* self pid */
	src.nl_groups = 0x1;
	if (bind(fd, (struct sockaddr *)&src, sizeof(src)) < 0) {
		perror("bind");
		ret = 1;
		goto out_close_fd;
	}

	if (nldummy_send(fd, buf, sizeof(buf))) {
		ret = 1;
		goto out_close_fd;
	}

	if (nldummy_recv(fd, readback, sizeof(buf))) {
		ret = 1;
		goto out_close_fd;
	}
	for (i = 0; i < sizeof(buf); i++)
		expected[i] = buf[i] ^ 0xff;
	if (memcmp(expected, readback, sizeof(buf))) {
		dprintf(2, "%s: unexpected readback buffer\n", prog);
		printbuf("expected: ", expected, sizeof(buf));
		printbuf("actual  : ", readback, sizeof(buf));
		ret = 1;
		goto out_close_fd;
	}
	dprintf(2, "%s: passed readback test\n", prog);

	ret = 0;

out_close_fd:
	close(fd);
out_none:
	return ret;
}
