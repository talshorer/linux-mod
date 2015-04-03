/*
 * Execute a program with no parameters.
 * This program is used to work around the shell's fallback solution of trying
 * to run a program with itself as the executable if execve() returns ENOEXEC
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	#define prog (argv[1])
	char *prog_argv /*
			 * newline to avoid style checkers from complaining
			 * about this not being "static const". it won't work
			 * if it was.
			 */
			[] = {
		prog,
		NULL,
	};
	if (argc != 2) {
		dprintf(2, "usage: %s PROG\n", argv[0]);
		return 1;
	}
	if (execv(prog, prog_argv) < 0) {
		perror("execv");
		return 1;
	}
	/* should never get here. replacing program suposedly calls exit() */
	dprintf(
		2,
		"execv() succeeded yet flow continues! should never happen!\n",
	);
	return 1;
}
