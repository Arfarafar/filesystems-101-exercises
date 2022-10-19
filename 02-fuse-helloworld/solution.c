#include <solution.h>

#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>



static int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *ffi,  enum fuse_readdir_flags frf)
{
    (void)off, (void)ffi, (void)frf;
	if (strcmp(path, "/") != 0)
		return -ENOENT;
    
	filler(data, ".", NULL, 0, 0);
	filler(data, "..", NULL, 0, 0);
	filler(data, "hello", NULL, 0, 0);
	return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *ffi)
{
    (void)path, (void) ffi;
	size_t len;
	char file_contents[128] = "";

    snprintf(file_contents, 128, "hello, %d\n", fuse_get_context() -> pid);
    len = strlen(file_contents);
	if ((size_t)off < len) {
		if ((size_t)off + size > len)
			size = len - off;
		memcpy(buf, file_contents + off, size);
	} else
		size = 0;

	return size;
}

static int fs_open(const char *path, struct fuse_file_info *ffi)
{
	if (strncmp(path, "/hello", 10) != 0)
		return -ENOENT;

	if ((ffi->flags & 3) != O_RDONLY)
		return -EROFS;

	return 0;
}

static void* fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
	cfg->uid = getgid();
	cfg->umask = ~0400;
	cfg->gid = getuid();

	cfg->set_mode = 1;
    cfg->set_uid = 1;
	cfg->set_gid = 1;
    cfg->kernel_cache = 1;

	return NULL;
}

static int fs_getattr(const char *path, struct stat *st, struct fuse_file_info *ffi)
{
    (void)ffi;
    memset(st, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0400;
		st->st_nlink = 2;
	} else if (strcmp(path, "/hello") == 0) {
		st->st_mode = S_IFREG | 0400;
		st->st_nlink = 1;
		st->st_size = 10; // ""It is OK to report the size of "hello" that does not match the content.""

	} else {
		return -ENOENT;
	}

	return 0;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info * ffi)
{
    (void)buf, (void)size, (void)off, (void)ffi;
    if (strcmp(path, "/hello") == 0)
	{
		return -EROFS;
	}

	return -EINVAL;
}

static int fs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	(void)name, (void)flags, (void)value, (void)size, (void)flags;

	if (strcmp(path, "/hello") == 0)
	{
		return -EROFS;
	}

	return -EINVAL;
}


static int fs_rename(const char* oldpath, const char* newpath, unsigned int flags)
{
	(void)newpath, (void)flags;

	if (strcmp(oldpath, "/hello") == 0)
	{
		return -EROFS;
	}

	return -EINVAL;
}


static int fs_unlink(const char* path)
{
	(void)path;
	return -EROFS;
}

static int fs_removexattr(const char* path, const char* name)
{
	(void)name;

	if (strcmp(path, "/hello") == 0)
	{
		return -EROFS;
	}

	return -EINVAL;
}


static const struct fuse_operations hellofs_ops = {
	.readdir = fs_readdir,
	.read = fs_read,
	.open = fs_open,
    .init = fs_init,
    .getattr = fs_getattr,
    .write = fs_write,
    .setxattr = fs_setxattr,
    .rename = fs_rename,
    .unlink = fs_unlink,
    .removexattr = fs_removexattr,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
