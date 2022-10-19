#pragma once

/**
   Implement this function to copy the content of an inode @inode_nr
   to a file descriptor @out. A file at @inode_nr may be a sparse file.
   @img is a file descriptor of an open ext2 image.

   It suffices to support single- and double-indirect blocks.

   If a copy was successful, return 0. If an error occurred during
   a read or a write, return -errno.
*/
int dump_file(int img, int inode_nr, int out);
