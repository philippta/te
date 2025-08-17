#include "rope.h"
#include <err.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void debug(const char *fmt, ...)
{
	FILE *f = fopen("debug.log", "a"); // "a" means append
	if (!f)
		return; // could also handle error more explicitly

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

#define BENCH(N, code_block)                                                                                           \
	do {                                                                                                           \
		mach_timebase_info_data_t _info;                                                                       \
		mach_timebase_info(&_info);                                                                            \
		uint64_t _start = mach_absolute_time();                                                                \
		for (int _i = 0; _i < (N); _i++) {                                                                     \
			code_block;                                                                                    \
		}                                                                                                      \
		uint64_t _end	     = mach_absolute_time();                                                           \
		uint64_t _elapsed_ns = (_end - _start) * _info.numer / _info.denom;                                    \
		debug("Benchmark (%d runs): total = %8llu ns, average = "                                              \
		      "%8llu ns\n",                                                                                    \
		      (N), (unsigned long long)_elapsed_ns, (unsigned long long)(_elapsed_ns / (N)));                  \
	} while (0)

struct arena {
	char  *buf;
	size_t size;
	size_t used;
};

#define TERMINAL_MODE_ALTERNATE "\e[?1049h", 8
#define TERMINAL_MODE_NORMAL	"\e[?1049l\e[1000;1H", 15
#define TERMINAL_CURSOR_HIDE	"\e[?25l", 6
#define TERMINAL_CURSOR_SHOW	"\e[?25h", 6
#define TERMINAL_CURSOR_RESET	"\e[1;1H", 6
#define TERMINAL_FRAME_START	"\e[?25l\e[1;1H", 12
#define TERMINAL_FRAME_END	"\e[1;1H\e[?25h", 12

struct terminal_config {
	int	       fd;
	struct termios termios;
	int	       window_rows;
	int	       window_cols;
};

int terminal_init(struct terminal_config *term);
int terminal_get_window_size(struct terminal_config *term, int *rows, int *cols);
int terminal_cleanup(struct terminal_config *term);

int terminal_init(struct terminal_config *term)
{
	term->fd = open("/dev/tty", O_RDWR);
	if (term->fd < 0) {
		return -1;
	}

	if (tcgetattr(term->fd, &term->termios) == -1) {
		close(term->fd);
		return -1;
	}

	struct termios raw = term->termios;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);
	raw.c_cc[VMIN]	= 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(term->fd, TCSAFLUSH, &raw) == -1) {
		terminal_cleanup(term);
		return -1;
	}

	if (terminal_get_window_size(term, &term->window_rows, &term->window_cols) == -1) {
		terminal_cleanup(term);
		return -1;
	}

	if (write(term->fd, TERMINAL_MODE_ALTERNATE) == -1) {
		terminal_cleanup(term);
		return -1;
	}
	return 0;
}

int terminal_cleanup(struct terminal_config *term)
{
	if (write(term->fd, TERMINAL_MODE_NORMAL) == -1) {
		return -1;
	}
	if (tcsetattr(term->fd, TCSAFLUSH, &term->termios) == -1) {
		return -1;
	}
	if (close(term->fd) == -1) {
		return -1;
	}
	return 0;
}

int terminal_get_window_size(struct terminal_config *term, int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(term->fd, TIOCGWINSZ, &ws) == -1) {
		return -1;
	}
	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 0;
}

struct bounds {
	int x, y, w, h;
};

struct render_context {
	char *screen_buffer;
	int   rows;
	int   cols;
};

struct display_buffer {
	char *cells;
	int   rows;
	int   cols;
};

void display_buffer_cleanup(struct display_buffer *display)
{
	if (display->cells != NULL) {
		free(display->cells);
	}
}

int  render_context_init(struct render_context *ctx, int rows, int cols);
void render_context_clear(struct render_context *ctx);
int  render_context_render(struct render_context *ctx, int fd);
int  render_context_write_display_buffer(struct render_context *ctx, struct display_buffer *display, int row, int col);
void render_context_cleanup(struct render_context *ctx);

int render_context_init(struct render_context *ctx, int rows, int cols)
{
	ctx->screen_buffer = malloc(rows * cols);
	if (ctx->screen_buffer == NULL) {
		return -1;
	}
	ctx->rows = rows;
	ctx->cols = cols;
	return 0;
}

void render_context_clear(struct render_context *ctx)
{
	if (ctx->screen_buffer == NULL) {
		return;
	}
	memset(ctx->screen_buffer, ' ', ctx->rows * ctx->cols);
}

int render_context_write_display_buffer(struct render_context *ctx, struct display_buffer *display, int row, int col)
{
	for (int r = 0; r < display->rows; r++) {
		int screen_row = row + r;

		// Skip rows that are outside the screen bounds
		if (screen_row < 0 || screen_row >= ctx->rows) {
			continue;
		}

		for (int c = 0; c < display->cols; c++) {
			int screen_col = col + c;

			// Skip columns that are outside the screen bounds
			if (screen_col < 0 || screen_col >= ctx->cols) {
				continue;
			}

			int display_idx = r * display->cols + c;
			int screen_idx	= screen_row * ctx->cols + screen_col;

			ctx->screen_buffer[screen_idx] = display->cells[display_idx];
		}
	}

	return 0;
}

int render_context_render(struct render_context *ctx, int fd)
{
	if (write(fd, TERMINAL_FRAME_START) == -1) {
		return -1;
	}
	if (write(fd, ctx->screen_buffer, ctx->rows * ctx->cols) == -1) {
		return -1;
	}
	if (write(fd, TERMINAL_FRAME_END) == -1) {
		return -1;
	}
	return 0;
}

void render_context_cleanup(struct render_context *ctx)
{
	if (ctx->screen_buffer != NULL) {
		free(ctx->screen_buffer);
		ctx->screen_buffer = NULL;
	}
}

struct file_buffer {
	rope *rope;
};

int  file_buffer_init_from_file(struct file_buffer *file, char *pathname);
void file_buffer_get_str(struct file_buffer *file, char **str, int *str_len);
void file_buffer_to_display_buffer(struct file_buffer *file, struct display_buffer *display, int rows, int cols);
void file_buffer_cleanup(struct file_buffer *file);

int file_buffer_init_from_file(struct file_buffer *file, char *pathname)
{
	struct stat st;
	if (stat(pathname, &st) == -1) {
		return -1;
	}

	int fd = open(pathname, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	char *buf = malloc(st.st_size + 1); // +1 for null terminator
	if (buf == NULL) {
		close(fd);
		return -1;
	}

	if (read(fd, buf, st.st_size) == -1) {
		free(buf);
		close(fd);
		return -1;
	}

	buf[st.st_size] = 0;
	close(fd);

	file->rope = rope_new();
	if (rope_insert(file->rope, 0, (uint8_t *)buf) != ROPE_OK) {
		free(buf);
		rope_free(file->rope);
		file->rope = NULL;
		return -1;
	}

	free(buf);
	return 0;
}

void file_buffer_get_str(struct file_buffer *file, char **str, int *str_len)
{
	*str_len = rope_char_count(file->rope);
	*str	 = (char *)rope_create_cstr(file->rope);
}

void file_buffer_to_display_buffer(struct file_buffer *file, struct display_buffer *display, int rows, int cols)
{
	display->cells = malloc(cols * rows);
	memset(display->cells, ' ', cols * rows);

	display->rows = rows;
	display->cols = cols;

	int   str_len = rope_byte_count(file->rope);
	char *str     = (char *)rope_create_cstr(file->rope);
	int   str_ofs = 0;

	for (int row = 0; row < rows && str_ofs < str_len; row++) {
		bool found_newline = false;
		for (int col = 0; col < cols && str_ofs < str_len; col++) {
			if (!found_newline) {
				char c = str[str_ofs];
				if (c == '\n') {
					found_newline = true;
					str_ofs++; // consume the newline
						   // rest of row stays as spaces (from memset)
				}
				else {
					display->cells[row * cols + col] = c;
					str_ofs++;
				}
			}
			// if found_newline is true, we just leave spaces for rest of row
		}
	}

	free(str);
}

void file_buffer_cleanup(struct file_buffer *file)
{
	if (file->rope != NULL) {
		rope_free(file->rope);
	}
}

struct status_line {
	char *mode;
	int   mode_len;
	char *file;
	int   file_len;
	int   cursor_row;
	int   cursor_col;
};

void status_line_to_display_buffer(struct status_line *sl, struct display_buffer *display, int cols)
{
	display->cells = malloc(cols);
	memset(display->cells, ' ', cols);

	display->rows = 1;
	display->cols = cols;

	int offset = 0;

	memcpy(display->cells + offset, sl->mode, MIN(cols - offset, sl->mode_len));
	offset += MIN(cols - offset, sl->mode_len);

	// Space between mode and filename
	offset += 4;

	memcpy(display->cells + offset, sl->file, MIN(cols - offset, sl->file_len));
	offset += MIN(cols - offset, sl->file_len);

	char str[32] = {};
	int  str_len = snprintf(str, sizeof(str), "%d,%d", sl->cursor_row, sl->cursor_col);

	// Right align cursor position
	offset = cols - str_len;
	if (offset < 0) {
		offset = 0;
	}

	memcpy(display->cells + offset, str, MIN(cols - offset, str_len));
	offset += MIN(cols - offset, str_len);
}

int main(void)
{
	struct terminal_config term	   = {};
	struct render_context  render_ctx  = {};
	struct file_buffer     file_buffer = {};

	if (terminal_init(&term) == -1) {
		err(EXIT_FAILURE, "terminal init");
	}

	if (render_context_init(&render_ctx, term.window_rows, term.window_cols) == -1) {
		terminal_cleanup(&term);
		err(EXIT_FAILURE, "render context init");
	}

	if (file_buffer_init_from_file(&file_buffer, "dummyfile.txt") == -1) {
		render_context_cleanup(&render_ctx);
		terminal_cleanup(&term);
		err(EXIT_FAILURE, "file buffer init");
	}

	render_context_clear(&render_ctx);

	struct display_buffer file_display = {};
	file_buffer_to_display_buffer(&file_buffer, &file_display, term.window_rows - 2, term.window_cols);
	render_context_write_display_buffer(&render_ctx, &file_display, 0, 0);

	struct status_line status_line = {
	    .mode	= "NORMAL",
	    .mode_len	= 6,
	    .file	= "foo.c",
	    .file_len	= 5,
	    .cursor_row = 10,
	    .cursor_col = 50,

	};

	struct display_buffer status_line_display = {};
	status_line_to_display_buffer(&status_line, &status_line_display, term.window_cols);
	render_context_write_display_buffer(&render_ctx, &status_line_display, term.window_rows - 2, 0);

	render_context_render(&render_ctx, term.fd);

	display_buffer_cleanup(&file_display);
	display_buffer_cleanup(&status_line_display);

	char c;
	read(term.fd, &c, 1);

	render_context_cleanup(&render_ctx);
	terminal_cleanup(&term);
}
