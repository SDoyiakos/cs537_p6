#define FUSE_USE_VERSION 30
#define FILE_NAME_MAX 28
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` -DHAVE_SETXATTR fusexmp.c -o fusexmp
*/

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "wfs.h"
#include <stdint.h>


static int * disks;
static unsigned char** mappings;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;
static struct wfs_inode *current_inode;

struct PathListNode {
	char* data;
	struct PathListNode* next;
};

int checkDBitmap(unsigned int inum) {

	int byte_dist = inum/8; // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* data_bitmap = mappings[0] + superblocks[0]->d_bitmap_ptr;
	unsigned char bit_val;

	data_bitmap+= byte_dist; // Go byte_dist bytes over
	bit_val = *data_bitmap;
	bit_val &= (1<<offset); // Shift over offset times
	if(bit_val > 0) {
		printf("Bit Val is %d\n", bit_val);
		return 1;
	}
	else {
		return 0;
	}
}

int checkIBitmap(unsigned int inum) {

	int byte_dist = inum/8; // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* inode_bitmap = mappings[0] + superblocks[0]->i_bitmap_ptr;
	unsigned char bit_val;

	inode_bitmap+= byte_dist; // Go byte_dist bytes over
	bit_val = *inode_bitmap;
	bit_val &= (1<<offset); // Shift over offset times
	if(bit_val > 0) {
		printf("Bit Val is %d\n", bit_val);
		return 1;
	}
	else {
		return 0;
	}
}

int markbitmap_d(unsigned int bnum, int used) {

	int byte_dist = bnum/8; // how many byes away from start bnum is
	unsigned char offset = bnum % 8; // We want to start at lower bits
	unsigned char* blocks_bitmap = mappings[0] + superblocks[0]->d_bitmap_ptr;

	blocks_bitmap+= byte_dist; // Go byte_dist bytes over
	if(used != 1){
		unsigned char mask = 1;
		mask = mask<<offset;
		mask = ~mask;
		*blocks_bitmap &= mask;
		return 0;
	}
	*blocks_bitmap = *blocks_bitmap | (unsigned char) used<<offset;
	return 0;
}

int markbitmap_i(unsigned int inum, int used) {

	int byte_dist = inum/8; // how many bytes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* inode_bitmap = mappings[0] + superblocks[0]->i_bitmap_ptr;

	inode_bitmap+= byte_dist; // Go byte_dist bytes over
	if(used != 1){
		unsigned char mask = 1;
		mask = mask<<offset;
		mask = ~mask;
		*inode_bitmap &= mask;
		return 0;
	}
	*inode_bitmap = *inode_bitmap | (unsigned char) used<<offset;
	return 0;
}

struct wfs_inode* iget(unsigned int inum) {
	struct wfs_inode* ret_val;
	if(checkIBitmap(inum) == 0) {
		printf("INode is not allocated\n");
		return NULL;
	}

	// Go to inode offset
	ret_val = (struct wfs_inode*)(mappings[0] + superblocks[0]->i_blocks_ptr + (inum) * BLOCK_SIZE);
	return ret_val;
}

int findFreeInode() {

    // Iterate over all entries until one that isnt mapped is found
    for(int i = 0;i < (int)superblocks[0]->num_inodes;i++) {
        if(checkIBitmap(i) == 0) {
            return i;
        }
    }
    return -1; // Return -1 if no open mappings are found
}

int findFreeData() {

    // Iterate over all entries until one that isnt mapped is found
    for(int i =0; i < (int)superblocks[0]->num_data_blocks;i++) {
        if(checkDBitmap(i) == 0) {
            return i;
        }
    }
    return -1; // Return -1 if no open mappings are found
}

/** allocateBlock
* Finds an open block and then returns its offset
**/
int allocateBlock() {
	int ret_val;
	int data_bit;

	// Find open spot
	data_bit = findFreeData();
	if(data_bit == -1) {
		return -1;
	}

	ret_val = BLOCK_SIZE * data_bit; // Offset is 512 * data_bit
	markbitmap_d(data_bit, 1); // Mark this as allocated
	return ret_val; // Returns first entry within block
}

/** allocateInode
* Finds an open inode and then returns its offset from inode ptr
**/
int allocateInode() {
	int ret_val;
	int data_bit;

	data_bit = findFreeInode();
	if(data_bit == -1) {
		return -1;
	}

	ret_val = BLOCK_SIZE * data_bit;
	markbitmap_i(data_bit, 1);
	return ret_val;
}

struct wfs_dentry* getDirent(off_t dir_offset) {
	return  (struct wfs_dentry*)(mappings[0] + superblocks[0]->d_blocks_ptr + dir_offset);
}

struct wfs_inode* navigateToInode(const char* path) {
	char* modifiable_path;
	return NULL;
}

off_t findOpenDir(struct wfs_inode* parent) {
	off_t entry_offset;
	for(int i =0;i < N_BLOCKS; i++) {

		// If block not allocated
		if(parent->blocks[i] == -1) {
			int block_offset;
			block_offset = allocateBlock();

			// If unable to allocate
			if(block_offset == -1) {
				return -1;
			}
			// If able to allocate then set its offset
			else {
				parent->blocks[i] = block_offset;
				entry_offset = parent->blocks[i];
				parent->size+=BLOCK_SIZE;
				return entry_offset;
			}
		}
		// Found an allocated block
		else {
			// Iterate over all entries for a free one in the parent->block[i]
			struct wfs_dentry* curr_entry;
			for(int j = 0; j < BLOCK_SIZE; j+=sizeof(struct wfs_dentry)) {
				
				curr_entry = getDirent(parent->blocks[i] + j); 
				if(curr_entry->num == 0) { // If current entry is not used
					return i+j; // Return offset of entry i(offset of block) + j(offset within block)
				}
			}
		}
	}
	return -1; // Return this when no open space is found
}

struct wfs_dentry* searchDir(struct wfs_inode* parent, char* dir_name) {

	// Iterates over all directory entries
	for(int i =0;i < N_BLOCKS;i++) {
		if(parent->blocks[i] != -1) { // Check if parent block is allocated

			// Iterate over all dirents
			for(int j = 0; j < BLOCK_SIZE; j+=sizeof(struct wfs_dentry)) {
				if(strcmp(getDirent(parent->blocks[i]+j)->name, dir_name) == 0) {
					return getDirent(parent->blocks[i]+j);
				}
			}
		}
	}
	return NULL;
}

static int wfs_mkdir(const char* path, mode_t mode) {
	char* modifiable_path = NULL; // Holds a path we can modify
	char** path_parts = NULL; // List of strings holding path parts
	int path_part_ct = 0;

	// Creating a path representation that can be modified 
	modifiable_path = malloc(sizeof(char) * (strlen(path) + 1));
	if(modifiable_path == NULL) {
		return -1;
	}
	strcpy(modifiable_path, path);

	// Tokenize on / while adding it to the list
	char* curr_part = strtok(modifiable_path, "/");
	while(curr_part != NULL) {
		path_part_ct++;

		// If no parts then allocate the list
		if(path_parts == NULL) {
			path_parts = malloc(sizeof(char*));
			if(path_parts == NULL) {
				return -1;
			}
		}	

		// Reallocate if this is already allocated
		else if(realloc(path_parts, sizeof(char*) * path_part_ct) == NULL) {
			return -1;
		} 
		
		path_parts[path_part_ct - 1] = curr_part; // Set index value
		curr_part = strtok(NULL, "/"); // Get next token
	}


	/** Navigating path **/
	char* curr_dir_name;
	struct wfs_inode* curr_inode = iget(0); // Start at root
	struct wfs_dentry* curr_dir;
	for(int i = 0; i < path_part_ct - 1;i++) {
		printf("Path part is : %s\n", path_parts[i]);
		// Retrieve a directory with the name that is within current inode
		curr_dir_name = path_parts[i];
		curr_dir = searchDir(curr_inode, curr_dir_name);
		if(curr_dir == NULL) {
			return -1;
		}

		// Get the inode of this and ensure its a dir
		curr_inode = iget(curr_dir->num);
		if((curr_inode->mode & S_IFDIR) == 0) {
			return -1;
		}
	}

	printf("Parent of new dir num is %d with nlinks %d\n", curr_inode->num, curr_inode->nlinks);

	off_t newdir_offset; // Offset of dirent in new dir				 
	struct wfs_dentry* new_entry; // New Dirent entry				 
	off_t newinode_offset; // Offset of new inode for new dir			 							 
	struct wfs_inode* new_inode; // New inode allocated for new dir			 
	                                                                                
	// Create child inode								 
	newinode_offset = allocateInode();						 
	new_inode = (struct wfs_inode*)(mappings[0] + superblocks[0]->i_blocks_ptr + newinode_offset); 
	new_inode->num = (newinode_offset / BLOCK_SIZE);				 
	new_inode->mode = S_IFDIR | mode;						 
	new_inode->uid = getuid();							 
	new_inode->gid = getgid();							 
	new_inode->size = 0;								 
	new_inode->nlinks = 1;								 
	new_inode->atim = time(0);							 
	new_inode->mtim = time(0);							 
	new_inode->ctim = time(0);							 
	//parent = findOpenDir(new_inode);						 
	for(int i = 0;i < N_BLOCKS;i++) {
		new_inode->blocks[i] = -1;
	}
                                                                       
	// Create entry in parent dir							 
	newdir_offset = findOpenDir(curr_inode); // Get an open entry in root		 
	new_entry =  getDirent(newdir_offset);	 
	strncpy(new_entry->name, path_parts[path_part_ct-1],MAX_NAME); // Copy name into dentry		 
	new_entry->num = new_inode->num;

	// Create entry in child dir
	newdir_offset = findOpenDir(new_inode);
	new_entry = getDirent(newdir_offset);
	new_entry->name[0] = '.';
	new_entry->name[1] = '.';
	new_entry->name[2] = '\0';
	new_entry->num = curr_inode->num;
											 
	return 0;
	
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			   off_t offset, struct fuse_file_info *fi)
{
	return 0;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev)
{

	return 0;
}

static int wfs_unlink(const char *path)
{
	return 0;
}

static int wfs_rmdir(const char *path)
{
	return 0;
}


static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	return 0;
}

static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	return 0;
}


static int wfs_getattr(const char* path, struct stat* stbuf)
{
	return 0;
}


static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};

void print_dbitmap(){
	int numdblocks = superblocks[0]->num_data_blocks;
	unsigned char* data_bitmap = mappings[0] + superblocks[0]->d_bitmap_ptr;
	for(int i = 0; i < numdblocks/8; i++){
		 for (int j = 0; j < 8; j++) {
			 printf("%d", !!((*(data_bitmap + i) << j) & 0x80));
		 }
		printf(" ");
	}
}

void print_ibitmap(){
	int numinodes = superblocks[0]->num_inodes;
	unsigned char* inode_bitmap = mappings[0] + superblocks[0]->i_bitmap_ptr;
	for(int i = 0; i < numinodes/8; i++){
		 for (int j = 0; j < 8; j++) {
			 printf("%d", !!((*(inode_bitmap + i) << j) & 0x80));
		 }
		printf(" ");
	}
	printf("\n");
}

unsigned char* bget(unsigned int bnum) {

	unsigned char* ret_val;
	// Checking if bitmap is allocated
	if(checkDBitmap(bnum) == 0) {
		printf("Data Block is not allocated\n");
		return NULL;
	}
	
	// Go to data offset
	ret_val = mappings[0] + superblocks[0]->d_blocks_ptr + (bnum * BLOCK_SIZE);
	return ret_val;
}

int mapDisks(int argc, char* argv[]) {
	int i = 1;

	// Reading disk images
	while(i <= argc && argv[i][0] != '-'){ 

		//read in the disks
		printf("argv[%d]: %s\n", i, argv[i]);
		numdisks++;
		disks = realloc(disks, sizeof(int) * numdisks);
		int fd = open(argv[i], O_RDWR);
		if(fd == -1){
			printf("failed to open file\n");
			//free(argv[i]);
			exit(1);
		}
		disks[i-1] = fd;
		i++;
	}
	
	// Allocate region for beginning ptr in mappings
	mappings = malloc(sizeof(void*)* numdisks); 
	if(mappings == NULL) {
		printf("Failed to allocate mapping addrs\n");
		exit(1);
	}

	// Allocate region in mem for superblock pointers
	superblocks = malloc(sizeof(struct wfs_sb*) * numdisks);
	if(superblocks == NULL) {
		printf("Failed to allocate superblocks\n");
		exit(1);
	}

	// Allocate region in mem for root of each image
	roots = malloc(sizeof(struct wfs_inode*) * numdisks);
	if(roots == NULL) {
		printf("Unable to allocate roots\n");
		exit(1);
	}

	// Map every disk into memory
	struct stat my_stat;
	for(int k = 0; k < numdisks;k++) {
		fstat(disks[k], &my_stat); // Get file information about disk image
		mappings[k] = mmap(NULL, my_stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE,disks[k], 0); // Map this image into mem

		// Check if mmap worked
		if(mappings[k] == MAP_FAILED) {
			printf("Error, couldn't mmap disk into memory\n");
			exit(1);
		}

		// Set superblock and root according to offsets
		superblocks[k] = (struct wfs_sb*)mappings[k];
		roots[k] = (struct wfs_inode*)((char*)superblocks[k] + superblocks[k]->i_blocks_ptr);
	}
	return i;
}

int main(int argc, char* argv[]){
	int new_argc; // Used to pass into fuse_main
	
	new_argc = argc - mapDisks(argc, argv); // Gets difference of what was already read vs what isnt
	new_argc = argc - new_argc; 
	char* new_argv[new_argc];

	for(int j = 0;j < new_argc; j++) {
		new_argv[j] = argv[1 + numdisks + j];
	}

	printf("Return value of mkdir is %d\n", wfs_mkdir("hello", S_IFDIR));
	printf("Return value of second mkdir is %d\n", wfs_mkdir("hello/world", S_IFDIR));	
	//return fuse_main(new_argc, new_argv, &ops, NULL);	
}
