#include <solution.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#define _STRUCT_TIMESPEC 1
#include <stdlib.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/dir.h>

#define FILEPATHLEN 256
#define PAGE 4096

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

	ntfs_inode *inode = ntfs_pathname_to_inode(volume, NULL, path);
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