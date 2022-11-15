#include <solution.h>


int block_transfer(int img, int out, long int block_size, long long int* remainfilesize, int upper_bound, uint32_t* blocks){

	char buf[block_size];
	for (int i = 0; i < upper_bound; i++) {
		int size = *remainfilesize > block_size ? block_size : *remainfilesize;

		if (block[i])
			if(pread(img, buf, size, block_size*blocks[i]) != size){
				return -errno;
			}
		else
			memset(buf, 0, size);

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


int dump_file(int img, int inode_nr, int out)
{
	struct ext2_super_block super_block = {};
	if(pread(img, (char*)&super_block, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) != sizeof(struct ext2_super_block))
		return -errno;

	long int block_size = 1024 << super_block.s_log_block_size;	

	return copy_file(img, out, &super_block, block_size, inode_nr);
}
