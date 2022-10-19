#include <solution.h>

#include <fuse.h>

static const struct fuse_operations hellofs_ops = {
	/* implement me */
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
