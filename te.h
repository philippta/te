#include "rope.h"
#include <stdlib.h>
#include <termios.h>

enum editor_mode {
	EDITOR_MODE_NORMAL,
	EDITOR_MODE_INSERT,
};

struct editor {
	int	       tty;
	struct termios termios_orig;

	enum editor_mode mode;

	int window_rows;
	int window_cols;

	int cursor_row;
	int cursor_col;

	char *screen;
	int   screen_size;

	rope *rope;

	char *file_buf;
	int   file_buf_size;
	int   file_line_count;
	int   file_line_digits;
};

struct bounds {
	int x, y, h, w;
};

int  editor_init(struct editor *editor);
int  editor_load_file(struct editor *editor, char *pathname);
int  editor_render(struct editor *editor);
void editor_render_line_numbers_in_bounds(struct editor *editor, int start, struct bounds bounds);
void editor_render_text_in_bounds(struct editor *editor, char *input, int input_len, struct bounds bounds);
void editor_set_cursor_position(struct editor *editor);
int  editor_close(struct editor *editor);
