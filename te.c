#include "te_tty.c"

#include <err.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void debug(const char *fmt, ...) {
	FILE *f = fopen("debug.log", "a"); // "a" means append
	if (!f)
		return; // could also handle error more explicitly

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

#define BENCH_NS_N(N, code_block)                                            \
	do {                                                                     \
		mach_timebase_info_data_t _info;                                     \
		mach_timebase_info(&_info);                                          \
		uint64_t _start = mach_absolute_time();                              \
		for (int _i = 0; _i < (N); _i++) {                                   \
			code_block;                                                      \
		}                                                                    \
		uint64_t _end = mach_absolute_time();                                \
		uint64_t _elapsed_ns = (_end - _start) * _info.numer / _info.denom;  \
		debug("Benchmark (%d runs): total = %8llu ns, average = %8llu ns\n", \
			  (N),                                                           \
			  (unsigned long long)_elapsed_ns,                               \
			  (unsigned long long)(_elapsed_ns / (N)));                      \
	} while (0)

enum mode {
	mode_normal,
	mode_insert,
};

struct editor {
	struct tty tty;

	char *text_buf;
	int text_buf_size;

	char *file_buf;
	int file_buf_size;
	int file_line_count;
	int file_line_digits;

	enum mode mode;

	int cursor_x;
	int cursor_y;
};

struct bounds {
	int x, y, h, w;
};

struct editor *editor = NULL;

int memcnt(char *buf, int size, char c) {
	int count = 0;
	char *p;
	while ((p = memchr(buf, c, size)) != NULL) {
		buf = p + 1;
		size -= (p - buf) + 1;
		count++;
	}
	return count;
}

void editor_resize(struct editor *editor) {
	tty_resize(&editor->tty);
}

int editor_init(struct editor *editor) {
	if (tty_open(&editor->tty) != 0) {
		return -1;
	}

	tty_mode_alternate(&editor->tty);

	return 0;
}

int editor_load_file(struct editor *editor, char *pathname) {
	struct stat st;
	if (stat(pathname, &st) != 0) {
		return -1;
	}

	puts(pathname);
	int f = open(pathname, O_RDONLY);
	if (f == -1) {
		return -1;
	}

	if (editor->file_buf == NULL || editor->file_buf_size != st.st_size) {
		editor->file_buf = malloc(st.st_size);
		editor->file_buf_size = st.st_size;
	} else {
		editor->file_buf = realloc(editor->file_buf, st.st_size);
		editor->file_buf_size = st.st_size;
	}

	if (read(f, editor->file_buf, editor->file_buf_size) != editor->file_buf_size) {
		return -1;
	}

	if (close(f) != 0) {
		return -1;
	}

	editor->file_line_count = memcnt(editor->file_buf, editor->file_buf_size, '\n');
	editor->file_line_digits = 0;
	for (int i = editor->file_line_count; i != 0; i /= 10)
		editor->file_line_digits++;

	return 0;
}

void format_line_number(char *buf, int buf_size, int num) {
	for (int i = buf_size - 1; i >= 0; i--) {
		buf[i] = (num % 10) + '0';
		num = num / 10;
	}
}

void editor_text_render(struct editor *editor, struct bounds bounds) {
	char *file = editor->file_buf;
	int offset = 0;
	int cols = editor->tty.cols;
	int ln_size = editor->file_line_digits;
	char ln_buf[ln_size];

	memset(ln_buf, ' ', ln_size);

	for (int i = 0; offset < editor->file_buf_size && i < bounds.h; i++) {
		format_line_number(ln_buf, editor->file_line_digits, i + 1);

		char *text = file + offset;
		char *text_end = memchr(text, '\n', editor->file_buf_size - offset);
		int text_len = text_end - text;

		memcpy(editor->text_buf + ((i + bounds.y) * cols) + bounds.x, ln_buf, MIN(bounds.w, ln_size));
		memcpy(editor->text_buf + ((i + bounds.y) * cols) + bounds.x + ln_size + 1, text, MIN(bounds.w - ln_size - 1, text_len));

		offset += (text_end - text) + 1;
	}
}

void editor_status_render(struct editor *editor, struct bounds bounds) {
	int cols = editor->tty.cols;
	memcpy(editor->text_buf + (bounds.y * cols), "dummyfile.txt", MIN(bounds.w, 13));

	switch (editor->mode) {
	case mode_normal:
		memcpy(editor->text_buf + (bounds.y * cols) + (bounds.w - 6), "NORMAL", MIN(bounds.w, 6));
		break;
	case mode_insert:
		memcpy(editor->text_buf + (bounds.y * cols) + (bounds.w - 6), "INSERT", MIN(bounds.w, 6));
		break;
	}
}

void editor_cursor_update(struct editor *editor) {
	editor->cursor_x = MAX(1, MIN(editor->cursor_x, editor->tty.cols));
	editor->cursor_y = MAX(1, MIN(editor->cursor_y, editor->tty.rows));

	char buf[16] = {};
	snprintf(buf, sizeof(buf) - 1, "\e[%d;%dH", editor->cursor_y, editor->cursor_x);

	tty_write(&editor->tty, buf, strlen(buf));
}

int editor_render(struct editor *editor, char bg) {
	if (editor->text_buf == NULL) {
		editor->text_buf = malloc(editor->tty.cols * editor->tty.rows);
		editor->text_buf_size = editor->tty.cols * editor->tty.rows;
	} else if (editor->text_buf_size != editor->tty.cols * editor->tty.rows) {
		editor->text_buf = realloc(editor->text_buf, editor->tty.cols * editor->tty.rows);
		editor->text_buf_size = editor->tty.cols * editor->tty.rows;
	}

	memset(editor->text_buf, bg == 0 ? ' ' : bg, editor->text_buf_size);

	struct bounds text_bounds = {
		.x = 0,
		.y = 0,
		.h = editor->tty.rows - 2, // (-status, -command)
		.w = editor->tty.cols,
	};

	struct bounds status_bounds = {
		.x = 0,
		.y = editor->tty.rows - 2,
		.h = 1,
		.w = editor->tty.cols,
	};

	editor_text_render(editor, text_bounds);
	editor_status_render(editor, status_bounds);

	tty_frame_start(&editor->tty);
	tty_write(&editor->tty, editor->text_buf, editor->text_buf_size);
	editor_cursor_update(editor);
	tty_cursor_show(&editor->tty);

	return 0;
}

void editor_close(struct editor *editor) {
	if (editor->file_buf != NULL) {
		free(editor->file_buf);
	}
	if (editor->text_buf != NULL) {
		free(editor->text_buf);
	}

	tty_mode_normal(&editor->tty);
	tty_close(&editor->tty);
	free(editor);
}
void handle_resize(int _) {
	editor_resize(editor);
}

int main(void) {
	editor = calloc(1, sizeof(struct editor));
	if (editor_init(editor) != 0) {
		err(EXIT_FAILURE, "Failed to init editor");
	}

	signal(SIGWINCH, handle_resize);
	handle_resize(0);

	if (editor_load_file(editor, "dummyfile.txt") != 0) {
		err(EXIT_FAILURE, "Failed to load file");
	}

	if (editor_render(editor, 0) != 0) {
		err(EXIT_FAILURE, "Failed to draw");
	}

	unsigned char c;
	for (;;) {
		c = tty_read_char(&editor->tty);
		int exit = 0;
		switch (c) {
		case 'h':
			editor->cursor_x--;
			break;
		case 'l':
			editor->cursor_x++;
			break;
		case 'j':
			editor->cursor_y++;
			break;
		case 'k':
			editor->cursor_y--;
			break;
		case 3:
			exit = 1;
			break;
		}

		if (exit) {
			break;
		}

		BENCH_NS_N(1, { editor_render(editor, 0); });
	}

	editor_close(editor);
}
