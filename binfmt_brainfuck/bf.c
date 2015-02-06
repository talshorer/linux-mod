#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define BRAINFUCK_ARRSIZE 0x4000
#define BRAINFUCK_MAX_LOOP_NEST_DEPTH 0x100

struct bf_ctx {
	int fd;
	char data[BRAINFUCK_ARRSIZE];
	unsigned dp;
	off_t stack[BRAINFUCK_MAX_LOOP_NEST_DEPTH];
	unsigned sp;
};

struct bf_cmd {
	char cmd;
	int (*fn)(struct bf_ctx *);
};

static inline off_t lastcmd(struct bf_ctx *ctx)
{
	return lseek(ctx->fd, 0, SEEK_CUR) - 1;
}

static inline int readone(struct bf_ctx *ctx, char *cmd)
{
	return read(ctx->fd, cmd, sizeof(*cmd));
}

static int incptr(struct bf_ctx *ctx)
{
	if (ctx->dp == BRAINFUCK_ARRSIZE - 1) {
		printf("data overflow error in cmd %lu\n", lastcmd(ctx));
		return 1;
	}
	ctx->dp++;
	return 0;
}

static int decptr(struct bf_ctx *ctx)
{
	if (ctx->dp == 0) {
		printf("data underflow error in cmd %lu\n", lastcmd(ctx));
		return 1;
	}
	ctx->dp--;
	return 0;
}

static int incval(struct bf_ctx *ctx)
{
	ctx->data[ctx->dp]++;
	return 0;
}

static int decval(struct bf_ctx *ctx)
{
	ctx->data[ctx->dp]--;
	return 0;
}

static int outchr(struct bf_ctx *ctx)
{
	printf("%c", ctx->data[ctx->dp]);
	return 0;
}

static int inchr(struct bf_ctx *ctx)
{
	system("stty raw");
	ctx->data[ctx->dp] = getchar();
	system("stty cooked");
	return 0;
}

static int startloop(struct bf_ctx *ctx)
{
	if (ctx->sp == BRAINFUCK_MAX_LOOP_NEST_DEPTH) {
		printf("stack overflow error in cmd %lu\n", lastcmd(ctx));
		return 1;
	}
	ctx->stack[ctx->sp] = lastcmd(ctx);
	if (ctx->data[ctx->dp]) {
		ctx->sp++;
	} else {
		unsigned count = 1;
		char cmd;
		while (1) {
			if (readone(ctx, &cmd) != sizeof(cmd)) {
				puts("unbalanced loops");
				return 1;
			}
			switch (cmd) {
			case '[':
				count++;
				break;
			case ']':
				if (!--count)
					return 0;
				break;
			}
		}
	}
	return 0;
}

static int endloop(struct bf_ctx *ctx)
{
	if (ctx->sp == 0) {
		printf("stack underflow error in cmd %lu\n", lastcmd(ctx));
		return 1;
	}
	ctx->sp--;
	if (ctx->data[ctx->dp])
		lseek(ctx->fd, ctx->stack[ctx->sp], SEEK_SET);
	return 0;
}

static struct bf_cmd cmds[] = {
	{ '>', incptr    },
	{ '<', decptr    },
	{ '+', incval    },
	{ '-', decval    },
	{ '.', outchr    },
	{ ',', inchr     },
	{ '[', startloop },
	{ ']', endloop   },
};
#define NCMDS (sizeof(cmds) / sizeof(cmds[0]))

static int execute_bf_program(char *filenmae)
{
	struct bf_ctx *ctx;
	char cmd;
	int ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		puts("malloc failed");
		ret = 1;
		goto out_none;
	}
	memset(ctx, 0, sizeof(*ctx));

	ctx->fd = open(filenmae, O_RDONLY);
	if (ctx->fd < 0) {
		perror("open");
		ret = 1;
		goto out_nofd;
	}

	while (1) {
		unsigned i;
		/* fetch */
		ret = readone(ctx, &cmd);
		if (ret < 0) {
			perror("read");
			ret = 1;
			goto out;
		} else if (ret == 0) {
			/* reached EOF. program is finished */
			break;
		}
		/* decode */
		for (i = 0; i < NCMDS; i++)
			if (cmds[i].cmd == cmd)
				break;
		if (i == NCMDS) {
			/* no such command. treat as comment */
			continue;
		}
		/* execute */
		ret = cmds[i].fn(ctx);
		if (ret)
			goto out;
	};
	ret = 0;
out:
	close(ctx->fd);
out_nofd:
	free(ctx);
out_none:
	return ret;
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("usage: %s FILENAME\n", argv[0]);
		return 1;
	}
	return execute_bf_program(argv[1]);
}
