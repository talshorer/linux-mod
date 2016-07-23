#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/serio.h>

#include "seriorepeater_uapi.h"

static int perform_test(int fd)
{
	static const char msg[] = "hello, world!\n";
	char readback[sizeof(msg)] = { 0 };

	sleep(1);
	if (write(fd, msg, sizeof(msg)) < 0) {
		perror("write");
		return 1;
	}
	if (read(fd, readback, sizeof(readback)) < 0) {
		perror("read");
		return 1;
	}
	if (memcmp(msg, readback, sizeof(msg))) {
		dprintf(2, "readback does not match sent message\n");
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 1;
	int mfd, sfd;
	unsigned int arg;
	unsigned long larg;
	char slave_name[sizeof("/dev/pts/XXX")];
	struct termios tty;
	pid_t pid;

	mfd = open("/dev/ptmx", O_RDWR);
	if (mfd < 0) {
		perror("open mfd");
		goto err_open_mfd;
	}
	arg = 0;
	if (ioctl(mfd, TIOCSPTLCK, &arg)) {
		perror("ioctl TIOCSPTLCK");
		goto err_ioctl_TIOCSPTLCK;
	}
	if (ioctl(mfd, TIOCGPTN, &arg)) {
		perror("ioctl TIOCGPTN");
		goto err_ioctl_TIOCGPTN;
	}

	sprintf(slave_name, "/dev/pts/%u", arg);
	sfd = open(slave_name, O_RDWR | O_NOCTTY);
	if (sfd < 0) {
		perror("open sfd");
		goto err_open_sfd;
	}
	if (tcgetattr(sfd, &tty)) {
		perror("tcgetattr");
		goto err_tcgetattr;
	}
	tty.c_cflag = CS8 | CREAD | HUPCL | CLOCAL;
	tty.c_iflag = IGNBRK | IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;
	cfsetispeed(&tty, B9600);
	cfsetospeed(&tty, B9600);
	if (tcsetattr(sfd, TCSANOW, &tty)) {
		perror("tcsetattr");
		goto err_tcsetattr;
	}
	arg = N_MOUSE;
	if (ioctl(sfd, TIOCSETD, &arg)) {
		perror("ioctl TIOCSETD");
		goto err_ioctl_TIOCSETD;
	}
	larg = SERIO_REPEATER;
	if (ioctl(sfd, SPIOCSTYPE, &larg)) {
		perror("ioctl SPIOCSTYPE");
		goto err_ioctl_SPIOCSTYPE;
	}

	pid = fork();
	switch (pid) {
	case -1:
		perror("fork");
		goto err_fork;
	case 0:
		read(sfd, NULL, 0);
		break;
	default:
		ret = perform_test(mfd);
		kill(pid, SIGTERM); /* terminate child */
		break;
	}

err_fork:
err_ioctl_SPIOCSTYPE:
	/* reset ldisc */
	arg = 0;
	ioctl(sfd, TIOCSETD, &arg);
err_ioctl_TIOCSETD:
err_tcsetattr:
err_tcgetattr:
	close(sfd);
err_open_sfd:
err_ioctl_TIOCGPTN:
err_ioctl_TIOCSPTLCK:
	close(mfd);
err_open_mfd:
	return ret;
}
