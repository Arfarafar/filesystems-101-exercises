#include <solution.h>

#include <fuse.h>

static const struct fuse_operations ext2_ops = {
	/* implement me */
};

int ext2fuse(int img, const char *mntp)
{
	(void) img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
