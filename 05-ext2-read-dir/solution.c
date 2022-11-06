#include <solution.h>
#include <ext2fs/ext2fs.h>
       #include <unistd.h>
#include <errno.h>



int dir_reader(int img, int out, long int block_size, int upper_bound, uint32_t* blocks){

	char buf[block_size];
	struct ext2_dir_entry_2* dir_entry = (struct ext2_dir_entry_2*) buf;

	for (int i = 0; i < upper_bound; i++) {
		if(blocks[i] == 0)
			return 0;
		if(pread(img, buf, size, block_size*blocks[i]) != block_size){
			return -errno;
		}

		int remainsize = block_size;
		char type = 0;
		while (remainsize){
			if(dir_entry -> file_type == EXT2_FT_REG_FILE)
				type = 'f';
			else if(dir_entry -> file_type == EXT2_FT_DIR)
				type = 'd';

			char filename[EXT2_NAME_LEN + 1];
			memcpy(filename, dir_entry -> name, dir_entry -> name_len);
			filename[dir_entry -> name_len] = '\0';

			report_file(dir_entry -> inode, type, filename);

			remainsize -= dir_entry -> rec_len;
			dir_entry = (struct ext2_dir_entry_2*) ((char*) dir_entry +  dir_entry -> rec_len);
		}
		
		
	}
	
	return 1;
}



int dump_dir(int img, int inode_nr)
{
	struct ext2_super_block super_block = {};
	if(pread(img, (char*)&super_block, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) != sizeof(struct ext2_super_block))
		return -errno;

	long int block_size = 1024 << super_block.s_log_block_size;	
	
	int addr_bg_descr = ((super_block.s_first_data_block+1)*block_size + sizeof(struct ext2_group_desc)*((inode_nr-1) / super_block.s_inodes_per_group));
	struct ext2_group_desc group_desc = {};
	if(pread(img, (char*)&group_desc, sizeof(struct ext2_group_desc), addr_bg_descr) != sizeof(struct ext2_group_desc))
		return -errno;

	struct ext2_inode inode = {};
	if(pread(img, (char*)&inode, sizeof(struct ext2_inode), group_desc.bg_inode_table*block_size + ((inode_nr-1) % super_block.s_inodes_per_group)*super_block.s_inode_size) != sizeof(struct ext2_inode))
		return -errno;
//----------------------------------------------------------------------------------------------------------------------------------------------------------------
	uint32_t* x1blocks = (uint32_t*)malloc(block_size);
	uint32_t* x2blocks = (uint32_t*)malloc(block_size);

	
	int res = dir_reader(img, out, block_size, EXT2_IND_BLOCK, inode.i_block);
	if(res <= 0)
		goto out;
//---------------------------------------------------------------------------------------------------------------------------------------	

	if(pread(img, (char*)x1blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]) != block_size){
		res = -errno;
			goto out;
	}
	res = dir_reader(img, out, block_size, block_size/4, x1blocks);
	if(res <= 0)
		goto out;

//------------------------------------------------------------------------------------------------------------------------------------------

	if(pread(img, (char*)x2blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK+1]) != block_size){
		res = -errno;
			goto out;
	}

	for (int j = 0; j < block_size/4; ++j)
	{
		if(pread(img, (char*)x1blocks, block_size, block_size * x2blocks[j]) != block_size){
			res = -errno;
			goto out;
		}
			
		res = dir_reader(img, out, block_size, block_size/4, x1blocks);
		if(res <= 0)
			goto out;
	}

out: free(x1blocks);
	 free(x2blocks);
	 return res;
}
