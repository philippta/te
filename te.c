#include "te_tty.c"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct editor {
	struct tty tty;
};

struct editor *editor = NULL;

void handle_resize(int _) {
	tty_refresh_size(&editor->tty);

	char buf[100] = {};
	sprintf(buf, "cols:%d rows:%d\n", editor->tty.cols, editor->tty.rows);
	tty_write(&editor->tty, buf, strlen(buf));
}

int main(void) {
	editor = malloc(sizeof(struct editor));
	unsigned char c;

	struct stat st;
	if (stat("dummyfile.txt", &st) == -1) {
		return -1;
	}
	int size = st.st_size;

	int f = open("dummyfile.txt", O_RDONLY);
	if (f == -1) {
		return -1;
	}
	char *filebuf = malloc(size);
	if (read(f, filebuf, size) != size) {
		return -1;
	}

	int offset = 0;
	for (int i = 0; offset < size; i++) {
		char buf[10] = {};
		sprintf(buf, "%d: ", i + 1);
		const char *start = filebuf + offset;
		const char *newline = memchr(start, '\n', size - offset);
		const int newlen = newline - start;
		write(1, buf, strlen(buf));
		write(1, start, newlen + 1);
		offset += newlen + 1;
	}

	// printf("%d\n", (int)(newline - filebuf));

	return 0;

	tty_open(&editor->tty);
	tty_mode_alternate(&editor->tty);

	signal(SIGWINCH, handle_resize);
	handle_resize(0);

	for (;;) {
		c = tty_read_char(&editor->tty);
		if (c == 3) {
			break;
		}

		// char buf[10] = {};
		// sprintf(buf, "%x\n", c);

		tty_write(&editor->tty, (char *)&c, 1);
	}

	tty_mode_normal(&editor->tty);
	tty_close(&editor->tty);
	free(editor);
}
