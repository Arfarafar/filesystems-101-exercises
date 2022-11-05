#include <solution.h>
#include <ext2fs/ext2fs.h>
       #include <unistd.h>
#include <errno.h>



int block_transfer(int img, int out, long int block_size, long long int* remainfilesize, int upper_bound, uint32_t* blocks){

	char* buf = (char*)malloc(block_size);
	for (int i = 0; i < upper_bound; i++) {
		int size = *remainfilesize > block_size ? block_size : *remainfilesize;
		if(pread(img, buf, size, block_size*blocks[i]) != size){
			free(buf);
			return -errno;
		}
		if(write(out, buf, size) != size){
			free(buf);
			return -errno;
		}
		*remainfilesize -= block_size;
		if (*remainfilesize <= 0){
			free(buf);
			return 0;
		}
	}
	free(buf);
	return 1;
}



int dump_file(int img, int inode_nr, int out)
{
	struct ext2_super_block super_block = {};
	if(pread(img, (char*)&super_block, sizeof(struct ext2_super_block), SUPERBLOCK_OFFSET) != sizeof(struct ext2_super_block))
		return -errno;

	long int block_size = 1024 << super_block.s_log_block_size;	
	
	int addr_ino_table = ((super_block.s_first_data_block+1)*block_size + sizeof(struct ext2_group_desc)*((inode_nr-1) / super_block.s_inodes_per_group) + 8);
	uint32_t ino_table;
	if(pread(img, (char*)&ino_table, 4, addr_ino_table) != 4)
		return -errno;

	struct ext2_inode inode = {};
	if(pread(img, (char*)&inode, sizeof(struct ext2_inode), ino_table*block_size + ((inode_nr-1) % super_block.s_inodes_per_group)*sizeof(struct ext2_inode)) != sizeof(struct ext2_inode))
		return -errno;
//----------------------------------------------------------------------------------------------------------------------------------------------------------------
	long long remainfilesize = ((long long)inode.i_size_high << 32L) + (long long)inode.i_size;
	uint32_t* x1blocks = (uint32_t*)malloc(block_size);
	uint32_t* x2blocks = (uint32_t*)malloc(block_size);

	
	int res = block_transfer(img, out, block_size, &remainfilesize, EXT2_IND_BLOCK, inode.i_block);
	if(res <= 0)
		goto out;
//---------------------------------------------------------------------------------------------------------------------------------------	

	if(pread(img, (char*)x1blocks, block_size, block_size * inode.i_block[EXT2_IND_BLOCK]) != block_size){
		res = -errno;
			goto out;
	}
	res = block_transfer(img, out, block_size, &remainfilesize, block_size/4, x1blocks);
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
			
		res = block_transfer(img, out, block_size, &remainfilesize, block_size/4, x1blocks);
		if(res <= 0)
			goto out;
	}

out: free(x1blocks);
	 free(x2blocks);
	 return res;
}

