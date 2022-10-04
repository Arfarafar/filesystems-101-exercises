#include <solution.h>

#include <stdio.h>
#include <string.h>

void report_process(pid_t pid, const char *exe, char **argv, char **envp)
{
	printf("pid %ld:\n", (long int)pid);
	printf("  exe = '%s'\n", exe);

	printf("  argv = [");
	for (char **x = argv; *x != NULL; ++x)
		printf("'%s', ", *x);
	printf("]\n");

	printf("  envp = [");
	for (char **x = envp; *x != NULL; ++x)
		printf("'%s', ", *x);
	printf("]\n");
}

void report_error(const char *path, int errno_code)
{
	fprintf(stderr, "failed to access '%s': %i (%s)\n", path, errno_code, strerror(errno_code));
}
