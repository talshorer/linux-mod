#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <linux/fcntl.h>

#include "bufhub_ioctl.h"

#define MODULE_NAME "bufhub"
#define BUFHUB_MISCDEV "/dev/" MODULE_NAME
#define BUFHUB_CLIPBOARD BUFHUB_MISCDEV "_clipboard"

static int _silent = 0;
static inline int bufhub_test_perror(char *msg)
{
	if (!_silent)
		perror(msg);
}
#define silent(expr)	\
({	                    \
	typeof(expr) __ret; \
	_silent = 1;        \
	__ret = (expr);     \
	_silent = 0;        \
	__ret;              \
})

static int open_miscdev(int *mfd)
{
	*mfd = open(BUFHUB_MISCDEV, O_RDONLY);
	if (*mfd < 0) {
		bufhub_test_perror("Failed to open miscdev");
		return 1;
	}
	return 0;
}

static int close_miscdev(int mfd)
{
	if(close(mfd) < 0) {
		bufhub_test_perror("Failed to close miscdev");
		return 1;
	}
	return 0;
}

static int create_clipboard(int mfd, unsigned int *cid)
{
	if(ioctl(mfd, BUFHUB_IOCCREATE, cid) < 0) {
		bufhub_test_perror("Failed to create clipboard");
		return 1;
	}
	return 0;
}

static int destroy_clipboard(int mfd, unsigned int cid)
{
	if(ioctl(mfd, BUFHUB_IOCDESTROY, &cid) < 0) {
		bufhub_test_perror("Failed to destroy clipboard");
		return 1;
	}
	return 0;
}

typedef char clipboard_name_t[sizeof(BUFHUB_CLIPBOARD) + 8];

static inline void clipboard_name(clipboard_name_t *buf, int cid)
{
	sprintf(*buf, "%s%d", BUFHUB_CLIPBOARD, cid);
}

static int clipboard_exists(int cid)
{
	clipboard_name_t buf;
	clipboard_name(&buf, cid);
	if (access(buf, F_OK) < 0)
		return 0;
	return 1;
}

static unsigned int get_max_clipboards(void)
{
	char buf[8];
	int pfd;
	unsigned int max_clipboards;
	pfd = open("/sys/module/" MODULE_NAME "/parameters/max_clipboards",
			O_RDONLY);
	if (pfd < 0)
		return 0;
	buf[read(pfd, buf, sizeof(buf) - 1)] = 0;
	close(pfd);
	sscanf(buf, "%u", &max_clipboards);
	return max_clipboards;
}

/* actual tests */

static int test_readback(void)
{
	/* TODO */
	return 1;
}

static int test_open_WRONLY_deletes_clipboard_buffer(void)
{
	/* TODO */
	return 1;
}

static int test_create_destroy_clipboard(void)
{
	int mfd;
	unsigned int cid;
	int ret = 1;
	if (open_miscdev(&mfd))
		goto out_none;
	if (create_clipboard(mfd, &cid))
		goto out_close_miscdev;
	if (!clipboard_exists(cid))
		goto out_destroy_clipboard;
	ret = 0;
out_destroy_clipboard:
	destroy_clipboard(mfd, cid);
	if (clipboard_exists(cid))
		ret = 1;
out_close_miscdev:
	close_miscdev(mfd);
out_none:
	return ret;
}

static int test_closing_miscdev_destroys_clipboards(void)
{
	int mfd;
	unsigned int cid;
	int ret = 1;
	if (open_miscdev(&mfd))
		goto out_none;
	if (create_clipboard(mfd, &cid))
		goto out_close_miscdev;
	ret = 0;
out_close_miscdev:
	close_miscdev(mfd);
	if (clipboard_exists(cid))
		ret = 1;
out_none:
	return ret;
}

static int test_closing_miscdev_does_not_destroy_open_clipbaord(void)
{
	/* TODO */
	return 1;
}

static int test_clipboard_destruction_fails_with_wrong_master(void)
{
	int mfd0, mfd1;
	unsigned int cid;
	int ret = 1;
	if (open_miscdev(&mfd0))
		goto out_none;
	if (open_miscdev(&mfd1))
		goto out_close_miscdev0;
	if (create_clipboard(mfd0, &cid))
		goto out_close_miscdev1;
	if (silent(!destroy_clipboard(mfd1, cid)))
		goto out_close_miscdev1;
	if (!clipboard_exists(cid))
		goto out_close_miscdev1;
	ret = 0;
out_close_miscdev1:
	close_miscdev(mfd1);
out_close_miscdev0:
	/* clipboard is destroyed by this call to close_miscdev */
	close_miscdev(mfd0);
out_none:
	return ret;
}

static int test_creation_fails_with_too_many_clipboards(void)
{
	int mfd;
	unsigned int cid;
	unsigned int max;
	int ret = 1;
	max = get_max_clipboards();
	if (open_miscdev(&mfd))
		goto out_none;
	while (max--)
		if (create_clipboard(mfd, &cid))
			goto out_close_miscdev;
	if (silent(!create_clipboard(mfd, &cid)))
		goto out_close_miscdev;
	ret = 0;
out_close_miscdev:
	close_miscdev(mfd);
out_none:
	return ret;
}

struct single_test {
	int (*test_fn)(void);
	char *name;
};

#define test_entry(func)	\
{	                        \
	.test_fn = func,        \
	.name = #func,          \
}

static struct single_test all_tests[] = {
/*	test_entry(test_readback),*/
/*	test_entry(test_open_WRONLY_deletes_clipboard_buffer),*/
	test_entry(test_create_destroy_clipboard),
	test_entry(test_closing_miscdev_destroys_clipboards),
/*	test_entry(test_closing_miscdev_does_not_destroy_open_clipbaord),*/
	test_entry(test_clipboard_destruction_fails_with_wrong_master),
	test_entry(test_creation_fails_with_too_many_clipboards),
};

int main(int argc, char *argv[])
{
	struct single_test *active;
	int i, err, ret = 0;
	for (i = 0; i < sizeof(all_tests) / sizeof(all_tests[0]); i++) {
		active = &all_tests[i];
		dprintf(2, "%s: running test %s\n", argv[0], active->name);
		err = active->test_fn();
		if (err)
			dprintf(2, "%s: test %s failed\n", argv[0], active->name);
		ret |= err;
	}
	return ret;
}
