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
	int row;
	int col;
	int width;
	int height;
};

struct render_context {
	char *screen_buffer;
	int   rows;
	int   cols;
};

int  render_context_init(struct render_context *ctx, int rows, int cols);
void render_context_clear(struct render_context *ctx);
int  render_context_render(struct render_context *ctx, int fd);
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

void file_buffer_render_to_context(struct file_buffer *file, struct render_context *ctx, struct bounds *bounds)
{
	int   str_len = rope_byte_count(file->rope);
	char *str     = (char *)rope_create_cstr(file->rope);
	int   str_ofs = 0;

	for (int row = 0; row < bounds->height && str_ofs < str_len; row++) {
		int screen_row = bounds->row + row;

		// Skip rows that are outside the screen bounds
		if (screen_row < 0 || screen_row >= ctx->rows) {
			// Still need to advance through the string for this row
			while (str_ofs < str_len && str[str_ofs] != '\n') {
				str_ofs++;
			}
			if (str_ofs < str_len && str[str_ofs] == '\n') {
				str_ofs++; // consume newline
			}
			continue;
		}

		bool found_newline = false;
		for (int col = 0; col < bounds->width && str_ofs < str_len; col++) {
			int screen_col = bounds->col + col;

			// Skip columns that are outside the screen bounds
			if (screen_col < 0 || screen_col >= ctx->cols) {
				if (!found_newline) {
					char c = str[str_ofs];
					if (c == '\n') {
						found_newline = true;
					}
					str_ofs++;
				}
				continue;
			}

			if (!found_newline) {
				char c = str[str_ofs];
				if (c == '\n') {
					found_newline = true;
					str_ofs++; // consume the newline
					// rest of row in bounds will be whatever was already in screen_buffer
				}
				else {
					int screen_idx		       = screen_row * ctx->cols + screen_col;
					ctx->screen_buffer[screen_idx] = c;
					str_ofs++;
				}
			}
			// if found_newline is true, we leave whatever was already in screen_buffer
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

void status_line_render_to_context(struct status_line *sl, struct render_context *ctx, struct bounds *bounds)
{
	// Only render if we have at least one row in the bounds
	if (bounds->height < 1) {
		return;
	}

	int screen_row = bounds->row;

	// Skip if the status line row is outside screen bounds
	if (screen_row < 0 || screen_row >= ctx->rows) {
		return;
	}

	int offset = 0;

	// Render mode
	for (int i = 0; i < sl->mode_len && offset < bounds->width; i++) {
		int screen_col = bounds->col + offset;
		if (screen_col >= 0 && screen_col < ctx->cols) {
			int screen_idx		       = screen_row * ctx->cols + screen_col;
			ctx->screen_buffer[screen_idx] = sl->mode[i];
		}
		offset++;
	}

	// Space between mode and filename (4 spaces)
	offset += 4;

	// Render filename
	for (int i = 0; i < sl->file_len && offset < bounds->width; i++) {
		int screen_col = bounds->col + offset;
		if (screen_col >= 0 && screen_col < ctx->cols) {
			int screen_idx		       = screen_row * ctx->cols + screen_col;
			ctx->screen_buffer[screen_idx] = sl->file[i];
		}
		offset++;
	}

	// Prepare cursor position string
	char str[32] = {};
	int  str_len = snprintf(str, sizeof(str), "%d,%d", sl->cursor_row, sl->cursor_col);

	// Right align cursor position within the bounds
	offset = bounds->width - str_len;
	if (offset < 0) {
		offset = 0;
	}

	// Render cursor position
	for (int i = 0; i < str_len && offset < bounds->width; i++) {
		int screen_col = bounds->col + offset;
		if (screen_col >= 0 && screen_col < ctx->cols) {
			int screen_idx		       = screen_row * ctx->cols + screen_col;
			ctx->screen_buffer[screen_idx] = str[i];
		}
		offset++;
	}
}

struct editor_state {
	int cursor_row;
	int cursor_col;
};

int editor_state_place_cursor(struct editor_state *state, int fd)
{
	char buf[32] = {};
	int  buf_len = snprintf(buf, sizeof(buf), "\e[%d;%dH", state->cursor_row + 1, state->cursor_col + 1);
	if (write(fd, buf, buf_len) == -1) {
		return -1;
	}
	return 0;
}

int main(void)
{
	struct terminal_config term	    = {};
	struct render_context  render_ctx   = {};
	struct file_buffer     file_buffer  = {};
	struct editor_state    editor_state = {};

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

	struct bounds file_buffer_bounds;
	file_buffer_bounds.col	  = 0;
	file_buffer_bounds.row	  = 0;
	file_buffer_bounds.width  = term.window_cols;
	file_buffer_bounds.height = term.window_rows - 2;

	file_buffer_render_to_context(&file_buffer, &render_ctx, &file_buffer_bounds);

	struct bounds status_line_bounds;
	status_line_bounds.col	  = 0;
	status_line_bounds.row	  = term.window_rows - 2;
	status_line_bounds.width  = term.window_cols;
	status_line_bounds.height = 1;

	struct status_line status_line;
	status_line.mode       = "NORMAL";
	status_line.mode_len   = 6;
	status_line.file       = "foo.c";
	status_line.file_len   = 5;
	status_line.cursor_row = 10;
	status_line.cursor_col = 50;

	status_line_render_to_context(&status_line, &render_ctx, &status_line_bounds);
	render_context_render(&render_ctx, term.fd);

	for (;;) {
		char c;
		if (read(term.fd, &c, 1) == -1) {
			err(EXIT_FAILURE, "read input");
		}

		bool should_exit = false;

		switch (c) {
		case 'h':
			editor_state.cursor_col = MAX(0, editor_state.cursor_col - 1);
			editor_state_place_cursor(&editor_state, term.fd);
			break;
		case 'j':
			editor_state.cursor_row = MIN(file_buffer_bounds.height - 1, editor_state.cursor_row + 1);
			editor_state_place_cursor(&editor_state, term.fd);
			break;
		case 'k':
			editor_state.cursor_row = MAX(0, editor_state.cursor_row - 1);
			editor_state_place_cursor(&editor_state, term.fd);
			break;
		case 'l':
			editor_state.cursor_col = MIN(file_buffer_bounds.width - 1, editor_state.cursor_col + 1);
			editor_state_place_cursor(&editor_state, term.fd);
			break;
		case 'q':
			should_exit = true;
			break;
		}

		if (should_exit) {
			break;
		}
	}

	render_context_cleanup(&render_ctx);
	terminal_cleanup(&term);
}
