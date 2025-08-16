#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define TTY_ALT_MODE	 "\e[?1049h", 8
#define TTY_NORM_MODE	 "\e[?1049l", 8
#define TTY_CURSOR_HIDE	 "\e[?25l", 6
#define TTY_CURSOR_SHOW	 "\e[?25h", 6
#define TTY_CURSOR_RESET "\e[1;1H", 6
#define TTY_FRAME_START	 "\e[?25l\e[1;1H", 12
#define TTY_FRAME_END	 "\e[1;1H\e[?25h", 12

int tty_open(struct termios *orig)
{
	int fd = open("/dev/tty", O_RDWR);
	if (fd < 0) {
		return -1;
	}

	if (tcgetattr(fd, orig) == -1) {
		close(fd);
		return -1;
	}

	struct termios raw = *orig;
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);
	raw.c_cc[VMIN]	= 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSAFLUSH, &raw) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

int tty_close(int fd, struct termios *orig)
{
	if (tcsetattr(fd, TCSAFLUSH, orig) == -1) {
		return -1;
	}
	if (close(fd) == -1) {
		return -1;
	}
	return 0;
}

int tty_get_window_size(int fd, int *rows, int *cols)
{
	struct winsize ws;
	if (ioctl(fd, TIOCGWINSZ, &ws) == -1) {
		return -1;
	}
	*rows = ws.ws_row;
	*cols = ws.ws_col;
	return 0;
}

// struct tty {
// 	int fd_in;
// 	int fd_out;
// 	int cols;
// 	int rows;
// 	struct termios termios;
// };

// int tty_open(struct tty *tty)
// {
// 	tty->fd_in = open("/dev/tty", O_RDONLY);
// 	if (tty->fd_in < 0) {
// 		return -1;
// 	}
//
// 	tty->fd_out = open("/dev/tty", O_WRONLY);
// 	if (tty->fd_out < 0) {
// 		close(tty->fd_in);
// 		return -1;
// 	}
//
// 	if (tcgetattr(tty->fd_in, &tty->termios) == -1) {
// 		close(tty->fd_in);
// 		close(tty->fd_out);
// 		return -1;
// 	}
//
// 	struct termios raw = tty->termios;
// 	raw.c_lflag &= ~(ECHO | ICANON | ISIG);
// 	raw.c_cc[VMIN] = 1;
// 	raw.c_cc[VTIME] = 0;
//
// 	if (tcsetattr(tty->fd_in, TCSAFLUSH, &raw) == -1) {
// 		close(tty->fd_in);
// 		close(tty->fd_out);
// 		return -1;
// 	}
//
// 	return 0;
// }

// unsigned char tty_read_char(struct tty *tty)
// {
// 	unsigned char c;
//
// 	if (read(tty->fd_in, &c, 1) != 1) {
// 		return -1;
// 	}
//
// 	return c;
// }
//
// void tty_write(struct tty *tty, char *buf, int n)
// {
// 	write(tty->fd_out, buf, n);
// }
//
// int tty_resize(struct tty *tty)
// {
// 	struct winsize ws;
// 	if (ioctl(tty->fd_out, TIOCGWINSZ, &ws) == -1) {
// 		return -1;
// 	}
// 	tty->cols = ws.ws_col;
// 	tty->rows = ws.ws_row;
// 	return 0;
// }
//
// void tty_close(struct tty *tty)
// {
// 	tcsetattr(tty->fd_in, TCSAFLUSH, &tty->termios);
// 	close(tty->fd_in);
// 	close(tty->fd_out);
// }
//
// void tty_mode_alternate(struct tty *tty) { tty_write(tty, "\e[?1049h", 8); }
//
// void tty_mode_normal(struct tty *tty) { tty_write(tty, "\e[?1049l", 8); }
//
// void tty_cursor_hide(struct tty *tty) { tty_write(tty, "\e[?25l", 6); }
//
// void tty_cursor_show(struct tty *tty) { tty_write(tty, "\e[?25h", 6); }
//
// void tty_cursor_reset(struct tty *tty) { tty_write(tty, "\e[1;1H", 6); }
//
// void tty_frame_start(struct tty *tty)
// {
// 	write(tty->fd_out, "\e[?25l\e[1;1H", 12);
// }
//
// void tty_frame_end(struct tty *tty)
// {
// 	write(tty->fd_out, "\e[1;1H\e[?25h", 12);
// }
