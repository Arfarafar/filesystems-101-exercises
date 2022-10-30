#include <solution.h>

#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	int img = open("img", O_RDONLY);
	int out = open("out", O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);

	if (img < 0)
		errx(1, "open(img) failed");
	if (out < 0)
		errx(1, "open(out) failed");

	int r = dump_file(img, "/hello", out);
	if (r < 0)
		errx(1, "dump_file() failed");

	close(out);
	close(img);

	return 0;
}
