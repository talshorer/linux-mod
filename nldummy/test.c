#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "nldummy_uapi.h"

char *prog;

static struct nlmsghdr *nldummy_alloc_header(size_t len)
{
	struct nlmsghdr *nlh;

	nlh = malloc(NLMSG_SPACE(len));
	if (!nlh) {
		dprintf(2, "%s: failed to allocate netlink header\n", prog);
		return NULL;
	}
	memset(nlh, 0, NLMSG_SPACE(len));
	return nlh;
}

static int nldummy_send(int fd, const char *buf, size_t len)
{
	int ret;
	struct nlmsghdr *nlh;

	nlh = nldummy_alloc_header(len);
	if (!nlh)
		goto out_none;
	nlh->nlmsg_len = NLMSG_SPACE(len);
	nlh->nlmsg_pid = getpid();
	/* if (nlmsg_type < NLMSG_MIN_TYPE), the kernel skips the message */
	nlh->nlmsg_type = NLMSG_MIN_TYPE;
	nlh->nlmsg_flags |= NLM_F_REQUEST;
	memcpy(NLMSG_DATA(nlh), buf, len);

	if (write(fd, nlh, nlh->nlmsg_len) < 0) {
		perror("write");
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
	struct nlmsghdr *nlh;

	nlh = nldummy_alloc_header(len);
	if (!nlh)
		goto out_none;

	if (read(fd, nlh, NLMSG_SPACE(len)) < 0) {
		perror("read");
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
