#define TTY_ALT_MODE	 "\e[?1049h", 8
#define TTY_NORM_MODE	 "\e[?1049l\e[1000;1H", 15
#define TTY_CURSOR_HIDE	 "\e[?25l", 6
#define TTY_CURSOR_SHOW	 "\e[?25h", 6
#define TTY_CURSOR_RESET "\e[1;1H", 6
#define TTY_FRAME_START	 "\e[?25l\e[1;1H", 12
#define TTY_FRAME_END	 "\e[1;1H\e[?25h", 12

int tty_open(struct termios *orig);
int tty_close(int fd, struct termios *orig);
int tty_get_window_size(int fd, int *cols, int *rows);
