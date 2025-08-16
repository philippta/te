#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

int memcnt(char *buf, char c, int size)
{
	int count = 0;
	for (int i = 0; i < size; i++) {
		if (buf[i] == c) {
			count++;
		}
	}
	return count;
}

void format_line_number(char *buf, int buf_size, int num)
{
	buf[buf_size - 1] = '0';
	for (int i = buf_size - 1; i >= 0 && num > 0; i--) {
		buf[i] = (num % 10) + '0';
		num    = num / 10;
	}
}

int digits(int x)
{
	int d = 0;
	for (; x > 0; d++)
		x /= 10;
	return d;
}
