#include <solution.h>

#include <stdio.h>
#include <string.h>

void report_file(const char *path)
{
	printf("%s\n", path);
}

void report_error(const char *path, int errno_code)
{
	fprintf(stderr, "failed to access '%s': %i (%s)\n", path, errno_code, strerror(errno_code));
}
