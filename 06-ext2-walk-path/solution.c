#include <solution.h>
#include <ext2fs/ext2fs.h>
       #include <unistd.h>
       #include <string.h>
#include <errno.h>
#include <sys/types.h>
       #include <sys/stat.h>


int dir_reader(int img, long int block_size, int upper_bound, uint32_t* blocks, const char* left_path, char entry_type, int entry_len){

	char buf[block_size];
	(void)entry_len;
	for (int i = 0; i < upper_bound; i++) {
		if(blocks[i] == 0)
			return -ENOENT;

		if(pread(img, buf, block_size, block_size*blocks[i]) != block_size){
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

			dir_entry = (struct ext2_dir_entry_2*) ((char*) (dir_entry) +  dir_entry -> rec_len);	
		}
		
	}
	
	return 1;
}


int block_transfer(int img, int out, long int block_size, long long int* remainfilesize, int upper_bound, uint32_t* blocks){

	char buf[block_size];
	for (int i = 0; i < upper_bound; i++) {
		int size = *remainfilesize > block_size ? block_size : *remainfilesize;
		if(pread(img, buf, size, block_size*blocks[i]) != size){
			return -errno;
		}
		if(write(out, buf, size) != size){
			return -errno;
		}
		*remainfilesize -= block_size;
		if (*remainfilesize <= 0){
			return 0;
		}
	}
	
	return 1;
}



long int inode_offset(int img, struct ext2_super_block* super_block, long int block_size, int inode_nr){

	int addr_bg_descr = ((super_block -> s_first_data_block+1)*block_size + sizeof(struct ext2_group_desc)*((inode_nr-1) / super_block -> s_inodes_per_group));
	struct ext2_group_desc group_desc = {};
	if(pread(img, (char*)&group_desc, sizeof(struct ext2_group_desc), addr_bg_descr) != sizeof(struct ext2_group_desc))
		return -errno;


	return group_desc.bg_inode_table*block_size + ((inode_nr-1) % super_block -> s_inodes_per_group)*super_block -> s_inode_size;
}


int Find_ino(int img, struct ext2_super_block* super_block, long int block_size, int inode_nr, const char* path){

	path = path + 1;
	if (strlen(path) == 0){
		return -ENOENT;
	}

	struct ext2_inode inode = {};
	int offset = inode_offset(img, super_block, block_size, inode_nr);
	if(offset < 0)
		return offset;
	if(pread(img, (char*)&inode, sizeof(struct ext2_inode), offset) != sizeof(struct ext2_inode))
		return -errno;
//----------------------------------------------------------------------------------------------------------------------------------------------------------------
	uint32_t x1blocks[block_size/4];
	uint32_t x2blocks[block_size/4];

	char* ch = strchr(path, '/');
	char entry_type = ch != NULL ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
	int entry_len = ch != NULL ? ch - path : (int)strlen(path);
	//printf("%d\n", entry_len);
	
	int res = dir_reader(img, block_size, EXT2_IND_BLOCK, inode.i_block, path, entry_type, entry_len);
	if(res <= 0)
		return res;
	if (res > 2)
		return ch == NULL ? res : Find_ino(img, super_block, block_size, res, path + entry_len);
//---------------------------------------------------------------------------------------------------------------------------------------	

	if(inode.i_block[EXT2_IND_BLOCK] == 0){
		return -ENOENT;
	}
	if(pread(img, (char*)x1blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]) != block_size){
		return -errno;
	}
	
	res = dir_reader(img, block_size, block_size/4, x1blocks, path, entry_type, entry_len);
	
	if(res <= 0)
		return res;
	if (res > 2)
		return ch == NULL ? res : Find_ino(img, super_block, block_size, res, path + entry_len);

//------------------------------------------------------------------------------------------------------------------------------------------

	if(inode.i_block[EXT2_IND_BLOCK + 1] == 0){
		return -ENOENT;
	}
	if(pread(img, (char*)x2blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK+1]) != block_size){
		return -errno;
			
	}

	for (int j = 0; j < block_size/4; ++j)
	{
		if(pread(img, (char*)x1blocks, block_size, block_size * x2blocks[j]) != block_size){
			return-errno;
			
		}
			
		res = dir_reader(img, block_size, block_size/4, x1blocks, path, entry_type, entry_len);
		if(res <= 0)
			return res;
		if (res > 2)
			return ch == NULL ? res : Find_ino(img, super_block, block_size, res, path + entry_len);
	}


	 return -ENOENT;
}


int copy_file(int img, int out, struct ext2_super_block* super_block, long int block_size, int inode_nr){

	struct ext2_inode inode = {};
	int offset = inode_offset(img, super_block, block_size, inode_nr);
	if(offset < 0)
		return offset;
	if(pread(img, (char*)&inode, sizeof(struct ext2_inode), offset) != sizeof(struct ext2_inode))
		return -errno;

	long long remainfilesize = ((long long)inode.i_size_high << 32L) + (long long)inode.i_size;
	uint32_t x1blocks[block_size/4];
	uint32_t x2blocks[block_size/4];

	
	int res = block_transfer(img, out, block_size, &remainfilesize, EXT2_IND_BLOCK, inode.i_block);
	if(res <= 0)
		return res;

	if(pread(img, (char*)x1blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]) != block_size){
		return -errno;
		
	}
	res = block_transfer(img, out, block_size, &remainfilesize, block_size/4, x1blocks);
	if(res <= 0)
		return res;


	if(pread(img, (char*)x2blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK+1]) != block_size){
		return -errno;
	}

	for (int j = 0; j < block_size/4; ++j)
	{
		if(pread(img, (char*)x1blocks, block_size, block_size * x2blocks[j]) != block_size)
			return -errno;

		res = block_transfer(img, out, block_size, &remainfilesize, block_size/4, x1blocks);
		if(res <= 0)
			return res;
	}

	return -EFBIG;
}






int dump_file(int img, const char *path, int out)
{

	if(*path != '/')
		return -EFAULT;

	struct ext2_super_block super_block = {};
	if(pread(img, (char*)&super_block, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) != sizeof(struct ext2_super_block))
		return -errno;

	long int block_size = 1024 << super_block.s_log_block_size;	
	
	int inode_nr = EXT2_ROOT_INO;

	inode_nr = Find_ino(img, &super_block, block_size, inode_nr, path);
	//printf("%d\n", inode_nr);
	if (inode_nr < 0)
		return inode_nr;

	return copy_file(img, out, &super_block, block_size, inode_nr);
}
