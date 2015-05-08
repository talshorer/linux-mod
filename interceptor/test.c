#include <stdio.h>
#include <fcntl.h>

#include "interceptor_uapi.h"

int main(int argc, char *argv[])
{
	if (argc != 2)
		goto usage;
	printf("%d\n", open(argv[1], O_STRLEN));
	return 0;
usage:
	dprintf(2, "usage: %s STR\n", argv[0]);
	return 1;
}
