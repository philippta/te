
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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

void debug(const char *fmt, ...);
int  memcnt(char *buf, int size, char c);
void format_line_number(char *buf, int buf_size, int num);
int  digits(int x);
