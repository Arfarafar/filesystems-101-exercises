#include <solution.h>

#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	int in = open("in", O_RDONLY);
	int out = open("out", O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);

	if (in < 0)
		errx(1, "open(in) failed");
	if (out < 0)
		errx(1, "open(out) failed");

	int r = copy(in, out);
	if (r < 0)
		errx(1, "copy() failed");

	close(out);
	close(in);

	return 0;
}
