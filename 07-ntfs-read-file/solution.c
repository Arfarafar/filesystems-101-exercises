#include <solution.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#define _STRUCT_TIMESPEC 1
#include <stdlib.h>
#include <string.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/dir.h>

#define FILEPATHLEN 256
#define PAGE 4096



ntfs_inode* pathname_to_inode(ntfs_volume *vol, ntfs_inode *parent,
		const char *pathname)
{
	u64 inum;
	int len, err = 0;
	char *p, *q;
	ntfs_inode *ni;
	ntfs_inode *result = NULL;
	ntfschar *unicode = NULL;
	char *ascii = NULL;

	if (!vol || !pathname) {
		errno = EINVAL;
		return NULL;
	}
	
	ntfs_log_trace("path: '%s'\n", pathname);
	
	ascii = strdup(pathname);
	if (!ascii) {
		ntfs_log_error("Out of memory.\n");
		err = ENOMEM;
		goto out;
	}

	p = ascii;
	/* Remove leading /'s. */
	while (p && *p && *p == PATH_SEP)
		p++;

	if (parent) {
		ni = parent;
	} else {

		ni = ntfs_inode_open(vol, FILE_root);
		if (!ni) {
			ntfs_log_debug("Couldn't open the inode of the root "
					"directory.\n");
			err = EIO;
			result = (ntfs_inode*)NULL;
			goto out;
		}
	}

	while (p && *p) {
		/* Find the end of the first token. */
		q = strchr(p, PATH_SEP);
		if (q != NULL) {
			*q = '\0';
		}

		len = ntfs_mbstoucs(p, &unicode);
		if (len < 0) {
			ntfs_log_perror("Could not convert filename to Unicode:"
					" '%s'", p);
			err = errno;
			goto close;
		} else if (len > NTFS_MAX_NAME_LEN) {
			err = ENAMETOOLONG;
			goto close;
		}
		inum = ntfs_inode_lookup_by_name(ni, unicode, len);

		if (inum == (u64) -1) {
			err = errno;
			ntfs_log_debug("Couldn't find name '%s' in pathname "
					"'%s'.\n", p, pathname);
			goto close;
		}

		if (ni != parent)
			if (ntfs_inode_close(ni)) {
				err = errno;
				goto out;
			}

		inum = MREF(inum);
		ni = ntfs_inode_open(vol, inum);
		if (!ni) {
			ntfs_log_debug("Cannot open inode %llu: %s.\n",
					(unsigned long long)inum, p);
			err = EIO;
			goto close;
		}
	
		free(unicode);
		unicode = NULL;

		if (q) *q++ = PATH_SEP; /* JPA */
		p = q;
		while (p && *p && *p == PATH_SEP)
			p++;
	}

	result = ni;
	ni = NULL;
close:
	if (ni && (ni != parent))
		if (ntfs_inode_close(ni) && !err)
			err = errno;
out:
	free(ascii);
	free(unicode);
	if (err)
		errno = err;
	return result;
}

int dump_file(int img, const char *path, int out)
{

	char img_path[FILEPATHLEN] = "";
	char img_name[FILEPATHLEN] = "";
	sprintf(img_path, "/proc/self/fd/%d", img);
	int sz = readlink(img_path, img_name, FILEPATHLEN);
	if (sz < 0)
		return sz;
	img_name[sz] = '\0';
	
	
	ntfs_volume* volume = ntfs_mount(img_name, NTFS_MNT_RDONLY);
	if (!volume)
		return -errno;

	ntfs_inode *inode = pathname_to_inode(volume, NULL, path);
	if (!inode){
		int tmp = -errno;
		ntfs_umount(volume, TRUE);
		return tmp;
	}

	ntfs_attr *attribute = ntfs_attr_open(inode, AT_DATA, NULL, 0);
	if (!attribute){
		int tmp = -errno;
		ntfs_inode_close(inode);
		ntfs_umount(volume, TRUE);
		return tmp;
	}


	s64 read = 0, wrote = 0, already_read = 0; 
	char buf[PAGE] = "";

	for(;;){
		read = ntfs_attr_pread(attribute, already_read, PAGE, buf);
		if (!read)
			break;
		if (read < 0)
			return -errno;
		
		wrote = write(out, buf, read);
		if (read - wrote)
			return -errno;

		already_read += read;
	}

	ntfs_attr_close(attribute);
	ntfs_inode_close(inode);
	ntfs_umount(volume, TRUE);

	
	return 0;
}