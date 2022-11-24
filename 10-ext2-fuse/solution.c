#include <solution.h>

#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <ext2fs/ext2fs.h>

       #include <sys/stat.h>


static int ext2img = 0;




static long int inode_offset(struct ext2_super_block* super_block, long int block_size, int inode_nr){

	int addr_bg_descr = ((super_block -> s_first_data_block+1)*block_size + sizeof(struct ext2_group_desc)*((inode_nr-1) / super_block -> s_inodes_per_group));
	struct ext2_group_desc group_desc = {};
	if(pread(ext2img, (char*)&group_desc, sizeof(struct ext2_group_desc), addr_bg_descr) != sizeof(struct ext2_group_desc))
		return -errno;


	return group_desc.bg_inode_table*block_size + ((inode_nr-1) % super_block -> s_inodes_per_group)*super_block -> s_inode_size;
}



static int init_super(struct ext2_super_block* super_block, long int* block_size)
{
	if(super_block == NULL || block_size == NULL){
		return -EINVAL;
	}

	if(pread(ext2img, (char*)super_block, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) != sizeof(struct ext2_super_block))
		return -errno;

	*block_size = 1024 << super_block -> s_log_block_size;	
	return 0;
}


static int dir_reader(long int block_size, int upper_bound, uint32_t* blocks, const char* left_path, char entry_type, int entry_len){

	char buf[block_size];
	(void)entry_len;
	for (int i = 0; i < upper_bound; i++) {
		if(blocks[i] == 0)
			return -ENOENT;

		if(pread(ext2img, buf, block_size, block_size*blocks[i]) != block_size){
			return -errno;
		}
		struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*) buf;

		int remainsize = block_size;
		
		while (remainsize > 0){
			remainsize -= dir_entry -> rec_len;
			if(!strncmp(dir_entry -> name, left_path, dir_entry -> name_len) && (dir_entry -> name_len == entry_len)){
				if(dir_entry -> file_type != entry_type){
				//if(dir_entry -> file_type == EXT2_FT_REG_FILE && entry_type == EXT2_FT_DIR){
					//printf("%s\n", left_path);
					return -ENOTDIR;
				}
				return dir_entry -> inode;
			}

			dir_entry = (struct ext2_dir_entry_2*) ((char*) (dir_entry) + dir_entry -> rec_len);	
		}
		
	}
	
	return 1;
}

#define SINGLE_ARG(...) __VA_ARGS__

#define PARSERNODE(func, args, commonreshandler, uncom1, uncom2, uncom3) 												\
	struct ext2_inode inode = {};																						\
	int offset = inode_offset(super_block, block_size, inode_nr);														\
	if(offset < 0)																										\
		return offset;																									\
	if(pread(ext2img, (char*)&inode, sizeof(struct ext2_inode), offset) != sizeof(struct ext2_inode))					\
		return -errno;																									\
	uint32_t x1blocks[block_size/4];																					\
	uint32_t x2blocks[block_size/4];																					\
	int res = func(block_size, EXT2_IND_BLOCK, inode.i_block, args); 													\
	commonreshandler;																									\
	uncom1;																												\
	if(pread(ext2img, (char*)x1blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]) != block_size)			\
		return -errno;																									\
	res = func(block_size, block_size/4, x1blocks, args);     															\
	commonreshandler;																									\
	uncom2;																												\
	if(pread(ext2img, (char*)x2blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK+1]) != block_size)			\
		return -errno;																									\
	for (int j = 0; j < block_size/4; ++j){																				\
		if(pread(ext2img, (char*)x1blocks, block_size, block_size * x2blocks[j]) != block_size)							\
			return-errno;																								\
		res = func(block_size, block_size/4, x1blocks, args);															\
		commonreshandler;																								\
		uncom3;																											\
	}


static int Find_ino(struct ext2_super_block* super_block, long int block_size, int inode_nr, const char* path, char need_dir){

	path = path + 1;
	if (strlen(path) == 0){
		if(need_dir)
			return inode_nr;
		return -ENOENT;
	}

	char* ch = strchr(path, '/');
	char entry_type = (ch != NULL) || need_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
	int entry_len = ch != NULL ? ch - path : (int)strlen(path);

	
	PARSERNODE(  dir_reader, SINGLE_ARG(path, entry_type, entry_len),
			    {if(res <= 0) return res;
				 if (res > 2) return ch == NULL ? res : Find_ino(super_block, block_size, res, path + entry_len, need_dir);},
				{if(inode.i_block[EXT2_IND_BLOCK] == 0) 
					return -ENOENT;},
				{if(inode.i_block[EXT2_IND_BLOCK + 1] == 0)
					return -ENOENT;},
				{})

	 return -ENOENT;
}

static int dirdatafill(long int block_size, int upper_bound, uint32_t* blocks, fuse_fill_dir_t filler, void *data){
	char buf[block_size];
	
	for (int i = 0; i < upper_bound; i++) {
		if(blocks[i] == 0)
			return 0;

		if(pread(ext2img, buf, block_size, block_size*blocks[i]) != block_size){
			return -errno;
		}
		struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*) buf;

		int remainsize = block_size;
		
		while (remainsize > 0){
			char filename[EXT2_NAME_LEN + 1];
			struct stat stbuf = {};
			memcpy(filename, dir_entry -> name, dir_entry -> name_len);
			filename[dir_entry -> name_len] = '\0';

			char type = dir_entry -> file_type;
			if(type == EXT2_FT_REG_FILE)
				stbuf.st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
			else if(type == EXT2_FT_DIR)
				stbuf.st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
			
			stbuf.st_ino = 0;// dir_entry -> inode;
			filler(data, filename, &stbuf, 0, 0);
			remainsize -= dir_entry -> rec_len;
			dir_entry = (struct ext2_dir_entry_2*) ((char*) (dir_entry) +  dir_entry -> rec_len);
		}	
	}
	return 1;
}


static int readdir(struct ext2_super_block* super_block, long int block_size, int inode_nr, void* data, fuse_fill_dir_t filler){

	PARSERNODE(  dirdatafill, SINGLE_ARG(filler, data),
			    {if(res <= 0) return res;},
				{if(inode.i_block[EXT2_IND_BLOCK] == 0) return 0;},
				{if(inode.i_block[EXT2_IND_BLOCK + 1] == 0) return 0;},
				{})

	return 0;
}


static int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *ffi,  enum fuse_readdir_flags frf)
{
    (void)off, (void)ffi, (void)frf;

    long int block_size;
	struct ext2_super_block super_block;
	int retcode = 0;
	if((retcode = init_super(&super_block, &block_size))){
		return retcode;
	}

	int inode_nr = Find_ino(&super_block, block_size, EXT2_ROOT_INO, path, 1);
	if (inode_nr < 0)
		return inode_nr;
    
	return readdir(&super_block, block_size, inode_nr, data, filler);
}


static int readfile(struct ext2_super_block* super_block, long int block_size, int inode_nr, char *buf, size_t size, off_t off){

	struct ext2_inode inode = {};																						
	int offset = inode_offset(super_block, block_size, inode_nr);														
	if(offset < 0)																										
		return offset;																									
	if(pread(ext2img, (char*)&inode, sizeof(struct ext2_inode), offset) != sizeof(struct ext2_inode))					
		return -errno;

	long long remainfilesize = ((long long)inode.i_size_high << 32L) + (long long)inode.i_size;
	if(off >= remainfilesize)
		return 0;
	
	if (remainfilesize - off < (int)size)
		size = remainfilesize - off;
	

	unsigned int startblocknumber = off / block_size;
	unsigned int offinsideblock = off % block_size;
	size = (int)size > block_size - offinsideblock ? (unsigned)(block_size - offinsideblock) : size;

	if (startblocknumber < EXT2_IND_BLOCK){
		
		if(pread(ext2img, buf, size, block_size*inode.i_block[startblocknumber]) != (int)size){
			return -errno;
		}
	}
	else if (startblocknumber < EXT2_IND_BLOCK + block_size/4){
		startblocknumber -= EXT2_IND_BLOCK;
		int mempage = 0;
		if (pread(ext2img, (char*) &mempage, 4, block_size*inode.i_block[EXT2_IND_BLOCK] + startblocknumber*4) != 4)
			return -errno;
		
		if(pread(ext2img, buf, size, block_size*mempage) != (int)size){
			return -errno;
		}
	}
	else {
		startblocknumber -= EXT2_IND_BLOCK + block_size/4;
		int ind = startblocknumber / (block_size/4);
		int mempagefirst = 0;
		if (pread(ext2img, (char*) &mempagefirst, 4, block_size*inode.i_block[EXT2_IND_BLOCK+1] + ind*4) != 4)
			return -errno;
		startblocknumber = startblocknumber % (block_size/4);
		int mempage = 0;
		if (pread(ext2img, (char*) &mempage, 4, block_size*mempagefirst + startblocknumber*4) != 4)
			return -errno;
		
		if(pread(ext2img, buf, size, block_size*mempage) != (int)size){
			return -errno;
		}
	}

	return size;
}


static int fs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *ffi)
{

    (void)path, (void) ffi;
    long int block_size;
	struct ext2_super_block super_block;
	int retcode = 0;
	if((retcode = init_super(&super_block, &block_size))){
		return retcode;
	}

	int inode_nr = Find_ino(&super_block, block_size, EXT2_ROOT_INO, path, 0);
	if (inode_nr < 0)
		return inode_nr;

	return readfile(&super_block, block_size, inode_nr, buf, size, off);
}

static int fs_open(const char *path, struct fuse_file_info *ffi)
{
	long int block_size;
	struct ext2_super_block super_block;
	int retcode = 0;
	if((retcode = init_super(&super_block, &block_size))){
		return retcode;
	}

	int inode_nr = Find_ino(&super_block, block_size, EXT2_ROOT_INO, path, 0);
	if (inode_nr < 0)
		return inode_nr;

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
    long int block_size;
	struct ext2_super_block super_block;
	int retcode = 0;
	if((retcode = init_super(&super_block, &block_size))){
		return retcode;
	}

	int inode_nr_file = 0;
	int inode_nr_dir = Find_ino(&super_block, block_size, EXT2_ROOT_INO, path, 1);

    memset(st, 0, sizeof(struct stat));
	if (inode_nr_dir > 0) {
		st->st_mode = S_IFDIR | 0400;
		st->st_nlink = 2;
		st -> st_ino = inode_nr_dir;
	} else if ((inode_nr_file = Find_ino(&super_block, block_size, EXT2_ROOT_INO, path, 0)) > 0) {
		st->st_mode = S_IFREG | 0400;
		st->st_nlink = 1;

		struct ext2_inode inode = {};																						
		int offset = inode_offset(&super_block, block_size, inode_nr_file);														
		if(offset < 0)																										
			return offset;																									
		if(pread(ext2img, (char*)&inode, sizeof(struct ext2_inode), offset) != sizeof(struct ext2_inode))					
			return -errno;

		st->st_size = ((long long)inode.i_size_high << 32L) + (long long)inode.i_size;
		
		st -> st_ino = inode_nr_file;
	} else {
		return -ENOENT;
	}

	return 0;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t off, struct fuse_file_info * ffi)
{
    (void)buf, (void)size, (void)off, (void)ffi, (void)path;
    return -EROFS;
}

static int fs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	(void)name, (void)flags, (void)value, (void)size, (void)flags, (void)path;

	return -EROFS;
	
}


static int fs_rename(const char *oldpath, const char *newpath, unsigned int flags)
{
	(void)newpath, (void)flags, (void)oldpath;

	return -EROFS;
}


static int fs_unlink(const char *path)
{
	(void)path;
	return -EROFS;
}

static int fs_removexattr(const char *path, const char *name)
{
	(void)name, (void)path;

	return -EROFS;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *ffi)
{
	(void)path, (void)mode, (void)ffi;
	return -EROFS;
}

static const struct fuse_operations ext2_ops = {
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
    .create = fs_create,
};

int ext2fuse(int img, const char *mntp)
{
	ext2img = img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
