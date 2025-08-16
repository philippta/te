#include "te.h"
#include "rope.h"
#include "tty.h"
#include "util.h"

#include <err.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct editor *editor = NULL;

// void editor_resize(struct editor *editor) { tty_resize(&editor->tty); }

int editor_init(struct editor *editor)
{
	editor->tty = tty_open(&editor->termios_orig);
	if (editor->tty == -1) {
		return -1;
	}

	if (tty_get_window_size(editor->tty, &editor->window_rows, &editor->window_cols) == -1) {
		return -1;
	};

	editor->screen	    = realloc(editor->screen, editor->window_rows * editor->window_cols);
	editor->screen_size = editor->window_rows * editor->window_cols;
	editor->rope	    = rope_new();

	write(editor->tty, TTY_ALT_MODE);

	return 0;
}

int editor_load_file(struct editor *editor, char *pathname)
{
	struct stat st;
	if (stat(pathname, &st) != 0) {
		return -1;
	}

	int fd = open(pathname, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	char *file_buf = malloc(st.st_size + 1);
	if (file_buf == NULL) {
		close(fd);
		return -1;
	}
	file_buf[st.st_size] = 0; // null terminator

	if (read(fd, file_buf, st.st_size) != st.st_size) {
		close(fd);
		return -1;
	}

	rope_insert(editor->rope, 0, (uint8_t *)file_buf);

	free(file_buf);
	close(fd);
	return 0;
}

void editor_render_line_numbers_in_bounds(struct editor *editor, int start, struct bounds bounds)
{
	char buf[bounds.w];
	memset(buf, ' ', bounds.w);

	for (int i = 0; i < bounds.h; i++) {
		format_line_number(buf, bounds.w, start + i);

		int screen_ofs = (i + bounds.y) * editor->window_cols + bounds.x;
		memcpy(editor->screen + screen_ofs, buf, bounds.w);
	}
}

void editor_render_text_in_bounds(struct editor *editor, char *input, int input_len, struct bounds bounds)
{
	char *input_end	  = input + input_len;
	int   current_row = 0;

	while (input < input_end && current_row < bounds.h) {
		char *next_line	 = memchr(input, '\n', input_end - input);
		int   screen_ofs = (current_row + bounds.y) * editor->window_cols + bounds.x;

		if (screen_ofs > editor->screen_size) {
			break;
		}

		memcpy(editor->screen + screen_ofs, input, MIN(bounds.w, next_line - input));

		input	    = next_line + 1;
		current_row = current_row + 1;
	}
}

// void editor_status_render(struct editor *editor, struct bounds bounds)
// {
// 	int cols = editor->tty.cols;
// 	memcpy(editor->screen_chars + (bounds.y * cols), "dummyfile.txt",
// 	       MIN(bounds.w, 13));
//
// 	switch (editor->mode) {
// 	case mode_normal:
// 		memcpy(editor->screen_chars + (bounds.y * cols) +
// 			   (bounds.w - 6),
// 		       "NORMAL", MIN(bounds.w, 6));
// 		break;
// 	case mode_insert:
// 		memcpy(editor->screen_chars + (bounds.y * cols) +
// 			   (bounds.w - 6),
// 		       "INSERT", MIN(bounds.w, 6));
// 		break;
// 	}
// }
//

int editor_render(struct editor *editor)
{
	// BENCH(1000, {
	//
	//
	//
	debug("render 1\n");
	size_t text_len = rope_char_count(editor->rope);
	char  *text	= (char *)rope_create_cstr(editor->rope);
	if (text == NULL) {
		return -1;
	}
	debug("render 2, %p, %d\n", text, text_len);

	// int text_lines	      = memcnt(text, '\n', text_len);
	// int text_lines_digits = digits(text_lines);
	// text_lines_digits = -1;
	debug("render 3\n");

	struct bounds text_bounds;
	// text_bounds.x = text_lines_digits + 1;
	text_bounds.x = 0;
	text_bounds.y = 0;
	// text_bounds.h = MIN(text_lines, editor->window_rows - 2); // (-status, -command)
	text_bounds.h = editor->window_rows; // (-status, -command)
	text_bounds.w = editor->window_cols - 3;
	debug("render 4\n");

	// struct bounds line_number_bounds;
	// line_number_bounds.x = MAX(text_bounds.x - text_lines_digits - 1, 0);
	// line_number_bounds.y = text_bounds.y;
	// line_number_bounds.h = text_bounds.h;
	// line_number_bounds.w = text_lines_digits;
	memset(editor->screen, ' ', editor->screen_size);
	editor_render_text_in_bounds(editor, text, text_len, text_bounds);
	// editor_render_line_numbers_in_bounds(editor, 1, line_number_bounds);
	debug("render 5\n");

	free(text);
	// });

	write(editor->tty, TTY_FRAME_START);
	write(editor->tty, editor->screen, editor->screen_size);
	debug("render 6\n");

	editor_set_cursor_position(editor);

	debug("render 7\n");
	return 0;
}

void editor_set_cursor_position(struct editor *editor)
{
	editor->cursor_row = MAX(0, MIN(editor->cursor_row, editor->window_rows - 1));
	editor->cursor_col = MAX(0, MIN(editor->cursor_col, editor->window_cols - 1));

	char buf[20] = {};
	snprintf(buf, sizeof(buf) - 1, "\e[%d;%dH\e[?25h", editor->cursor_row + 1, editor->cursor_col + 1);
	debug("CURSOR: row:%d col:%d\n", editor->cursor_row, editor->cursor_col);

	write(editor->tty, buf, strlen(buf));
}

int editor_close(struct editor *editor)
{
	if (editor->file_buf != NULL) {
		free(editor->file_buf);
	}
	if (editor->screen != NULL) {
		free(editor->screen);
	}

	write(editor->tty, TTY_NORM_MODE);
	tty_close(editor->tty, &editor->termios_orig);
	free(editor);

	return 0;
}

int main(void)
{
	// signal(SIGWINCH, handle_resize);
	// handle_resize(0);
	//
	editor = calloc(1, sizeof(struct editor));
	if (editor_init(editor) != 0) {
		perror("Failed to init editor");
		return -1;
	}

	if (editor_load_file(editor, "dummyfile.txt") != 0) {
		perror("Failed to load file");
		return -1;
	}

	if (editor_render(editor) != 0) {
		perror("Failed to render");
		return -1;
	}

	for (;;) {
		char c;
		if (read(editor->tty, &c, 1) != 1) {
			perror("Failed to read input");
			return -1;
		}

		bool should_exit = 0;
		switch (c) {
		case 'a':
			rope_insert(editor->rope, editor->cursor_col, (uint8_t *)"A");
			break;
		case 'x':
			rope_del(editor->rope, editor->cursor_col, 1);
			break;
		case 'h':
			editor->cursor_col--;
			break;
		case 'l':
			editor->cursor_col++;
			break;
		case 'j':
			editor->cursor_row++;
			break;
		case 'k':
			editor->cursor_row--;
			break;
		case 3:
			should_exit = true;
			break;
		}

		if (should_exit) {
			break;
		}

		if (editor_render(editor) != 0) {
			perror("Failed to render");
			return -1;
		}
	}

	editor_close(editor);
}
