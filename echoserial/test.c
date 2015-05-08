#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <fcntl.h>
#include <sys/time.h>

#define PORTNAME_LEN 32
#define ECHOSERIAL_PORTNAME "/dev/ttyE"

struct echoserial_baud {
	speed_t speed;
	unsigned int num;
};
#define baud_entry(n) {B##n, n}

static struct echoserial_baud all_speeds[] = {
	baud_entry(9600),
	baud_entry(19200),
	baud_entry(38400),
	baud_entry(57600),
	baud_entry(115200),
};
#define nspeeds (sizeof(all_speeds) / sizeof((all_speeds)f[0]))

static unsigned int nports;
static size_t bsize;
static char *prog;

static int echoserial_tcgetattr(int fd, struct termios *termios_p)
{
	memset(termios_p, 0, sizeof(*termios_p));
	if (tcgetattr(fd, termios_p)) {
		perror("Failed to get attributes");
		return 1;
	}
	return 0;
}

static int echoserial_tcsetattr(int fd, int optional_actions,
		const struct termios *termios_p)
{
	if (tcsetattr(fd, optional_actions, termios_p)) {
		perror("Failed to set attributes");
		return 1;
	}
	return 0;
}

static int readloop(int fd, char *buf, size_t size)
{
	size_t count;

	while (size) {
		count = read(fd, buf, size);
		if (count < 0) {
			if (errno == -EAGAIN)
				continue;
			else {
				perror("Failed to read from port");
				return 1;
			}
		}
		size -= count;
		buf += count;
	}
	return 0;
}

static void randomize_buffer(char *buf, size_t size)
{
	int fd = open("/dev/urandom", O_RDONLY);

	if (fd < 0) {
		unsigned char c = 0;

		while (size--)
			buf[size] = c++;
	} else
		readloop(fd, buf, size);
}

static int test_one_speed(int fd, struct echoserial_baud *baud)
{
	char *rbuf, *wbuf;
	struct termios tty;
	int err = 0;
	struct timeval t_start, t_end;
	unsigned long actual_time, expected_time;

	if (echoserial_tcgetattr(fd, &tty))
		return 1;
	cfsetispeed(&tty, baud->speed);
	cfsetospeed(&tty, baud->speed);
	if (echoserial_tcsetattr(fd, TCSANOW, &tty))
		return 1;
	wbuf = malloc(bsize * 2);
	if (!wbuf) {
		perror("Failed to allocate buffer");
		return 1;
	}
	rbuf = wbuf + bsize;
	randomize_buffer(wbuf, bsize);
	expected_time = bsize * 8 * 1000 / baud->num;
	gettimeofday(&t_start, NULL);
	if (write(fd, wbuf, bsize) != bsize) {
		err = 1;
		perror("Failed to write to port");
		goto out;
	}
	if (readloop(fd, rbuf, bsize)) {
		err = 1;
		goto out;
	}
	gettimeofday(&t_end, NULL);
	actual_time = (1000 * (t_end.tv_sec - t_start.tv_sec) +
			(t_end.tv_usec - t_start.tv_usec) / 1000);
	if (memcmp(wbuf, rbuf, bsize)) {
		err = 1;
		dprintf(2, "%s: failed readback test\n", prog);
		goto out;
	}
	/* we allow 200ms error */
	if (actual_time < expected_time || actual_time > expected_time + 200) {
		err = 1;
		dprintf(2, "%s: failed timing test\n", prog);
		goto out;
	}
	dprintf(2, "%s: times for baud %u: expected %lu actual %lu\n",
			prog, baud->num, expected_time, actual_time);
out:
	free(wbuf);
	return err;
}

static int test_one_port(unsigned int id)
{
	int fd;
	int err = 0;
	unsigned int i;
	char portname[PORTNAME_LEN];
	struct termios tty;

	sprintf(portname, "%s%u", ECHOSERIAL_PORTNAME, id);
	dprintf(2, "%s: running test on port %s with %u speeds\n",
			prog, portname, (unsigned int)nspeeds);
	fd = open(portname, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror("Failed to open port");
		return 1;
	}
	if (echoserial_tcgetattr(fd, &tty))
		return 1;
	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; /* 8-bit chars */
	tty.c_iflag &= ~IGNBRK; /* disable break processing */
	/* no signaling chars, no echo, no canonical processing */
	tty.c_lflag = 0;
	tty.c_oflag = 0; /* no remapping, no delays */
	tty.c_cc[VMIN] = 1; /* read blocks */
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); /* shut off xon/xoff ctrl */
	/* ignore modem controls, enable reading */
	tty.c_cflag |= (CLOCAL | CREAD);
	tty.c_cflag &= ~(PARENB | PARODD); /* shut off parity */
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
	cfmakeraw(&tty);
	if (echoserial_tcsetattr(fd, TCSANOW, &tty))
		return 1;
	for (i = 0; i < nspeeds; i++)
		err |= test_one_speed(fd, &all_speeds[i]);
	if (close(fd)) {
		perror("Failed to close port");
		err = 1;
	}
	return err;
}

int main(int argc, char *argv[])
{
	unsigned int id;
	int ret = 0;

	prog = argv[0];
	if (argc != 3)
		goto usage;
	nports = (unsigned int)strtol(argv[1], NULL, 10);
	bsize = (size_t)strtol(argv[2], NULL, 10);
	if (!nports || !bsize)
		goto usage;
	for (id = 0; id < nports; id++)
		ret |= test_one_port(id);
	return ret;
usage:
	dprintf(2, "usage: %s NPORTS BSIZE\n", prog);
	return 1;
}
