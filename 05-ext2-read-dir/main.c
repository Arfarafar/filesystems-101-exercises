#include <solution.h>

#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	int img = open("img", O_RDONLY);
	if (img < 0)
		errx(1, "open(img) failed");

	/* 2 is the inode nr. of the root directory */
	int r = dump_dir(img, 2);
	if (r < 0)
		errx(1, "dump_dir() failed");

	close(img);
	return 0;
}
