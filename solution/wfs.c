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

static int raid_mode;
static int * disks;
static int*  disk_size;
static unsigned char** mappings;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;

struct PathListNode {
	char* data;
	struct PathListNode* next;
};

typedef struct {
	char** path_components;
	int size;
} Path;

static int checkDBitmap(unsigned int inum, int disk) {
	int byte_dist = inum/8; // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* data_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;
	unsigned char bit_val;

	data_bitmap+= byte_dist; // Go byte_dist bytes over
	bit_val = *data_bitmap;
	bit_val &= (1<<offset); // Shift over offset times
	if(bit_val > 0) {
		return 1;
	}
	else {
		return 0;
	}
}

static int checkIBitmap(unsigned int inum, int disk) {

	int byte_dist = inum/8; // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;
	unsigned char bit_val;

	inode_bitmap+= byte_dist; // Go byte_dist bytes over
	bit_val = *inode_bitmap;
	bit_val &= (1 << offset); // Shift over offset times
	if(bit_val > 0) {
		return 1;
	}
	else {
		return 0;
	}
}

static int markbitmap_d(unsigned int bnum, int used, int disk) {

	int byte_dist = bnum/8; // how many byes away from start bnum is
	unsigned char offset = bnum % 8; // We want to start at lower bits
	unsigned char* blocks_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;

	blocks_bitmap+= byte_dist; // Go byte_dist bytes over
	if(used != 1){
		unsigned char mask = 1;
		mask = mask<<offset;
		mask = ~mask;
		*blocks_bitmap &= mask;
		return 0;
	}
	*blocks_bitmap = *blocks_bitmap | used<<offset;

	return 0;
}

static int markbitmap_i(unsigned int inum, int used, int disk) {

	int byte_dist = inum/8; // how many bytes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char* inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;

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

static int findFreeInode(int disk) {

    // Iterate over all entries until one that isnt mapped is found
    for(int i = 0;i < (int)superblocks[disk]->num_inodes;i++) {
        if(checkIBitmap(i, disk) == 0) {
            return i;
        }
    }
    return -1; // Return -1 if no open mappings are found
}

static int findFreeData(int disk) {

    // Iterate over all entries until one that isnt mapped is found
    for(int i =0; i < (int)superblocks[disk]->num_data_blocks;i++) {
        if(checkDBitmap(i, disk) == 0) {
            return i;
        }
    }
    return -1; // Return -1 if no open mappings are found
}

/** allocateBlock
* Finds an open block on the given disk and then returns its offset
**/
static int allocateBlock(int disk) {
	int ret_val;
	int data_bit;

	// Find open spot
	data_bit = findFreeData(disk);
	if(data_bit == -1) {
		return -1;
	}

	ret_val = BLOCK_SIZE * data_bit; // Offset is 512 * data_bit

	// Initialize new block to zero
	memset((unsigned char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + ret_val, 0, BLOCK_SIZE); 
	
	markbitmap_d(data_bit, 1, disk); // Mark this as allocated
	return ret_val; // Returns first entry within block
}

/** allocateInode
* Finds an open inode and then returns its offset from inode ptr
**/
static struct wfs_inode* allocateInode(int disk) {
	int ret_val;
	int data_bit;

	data_bit = findFreeInode(disk);
	if(data_bit == -1) {
		return NULL;
	}

	ret_val = BLOCK_SIZE * data_bit;
	markbitmap_i(data_bit, 1, disk);

	// Initialize inode
	struct wfs_inode* my_inode;
	my_inode = (struct wfs_inode*)((char*)mappings[disk] + superblocks[disk]->i_blocks_ptr + ret_val);
	my_inode->mode = 0x777; // RW for UGO
	my_inode->num = data_bit;
	my_inode->uid = getuid();
	my_inode->gid = getgid();
	my_inode->size = 0;
	my_inode->nlinks = 0;
	my_inode->atim = time(0);
	my_inode->mtim = time(0);
	my_inode->ctim = time(0);
	for(int i =0; i < N_BLOCKS;i++) {
		my_inode->blocks[i] = -1;
	}
	return my_inode;
}

/** splitPath
* Returns a Path struct which will contain an array of each entry in the path
**/
static Path* splitPath(char* path) {
	Path* ret_path;
	char* split_val;
	
	// Allocate the path
	ret_path = malloc(sizeof(Path));
	if(ret_path == NULL) {
		printf("Couldn't allocate path struct\n");
	}
	ret_path->size = 0;
	
	split_val = strtok(path, "/");
	
	while(split_val != NULL) {

		// If size = 0
		if(ret_path->size == 0) {
			ret_path->path_components = malloc(sizeof(char*));
			if(ret_path == NULL) {
				printf("Couldn't allocate path arr\n");
				return NULL;
			}
		}

		// If size > 0
		else {
			ret_path->path_components = realloc(ret_path->path_components, sizeof(char*) * (ret_path->size + 1));
			 if(ret_path->path_components == NULL) {
			 	printf("Error, realloc of path failed\n");
			 	return NULL;
			 }
		}

		// Allocate entry
		ret_path->path_components[ret_path->size] = strdup(split_val);
		if(ret_path->path_components[ret_path->size] == NULL) {
			printf("Error allocating the paths value\n");
			return NULL;
		}
		(ret_path->size)++;
		split_val = strtok(NULL, "/");
	}
	return ret_path;
}

/** getInode
* Returns the inode at a given index
**/
static struct wfs_inode* getInode(int inum, int disk) {

	// Check if its allocated
	if(checkIBitmap(inum, disk) == 0) {
		printf("Inode isn't allocated\n");
		return NULL;
	}	
	return (struct wfs_inode*)((char*)mappings[disk] + superblocks[disk]->i_blocks_ptr + (BLOCK_SIZE * inum));
} 

/** findOpenDir
* Finds an open directory in the parent directory
**/
static struct wfs_dentry* findOpenDir(struct wfs_inode* parent, int disk) {
	if((parent->mode & S_IFDIR) == 0) { // Check if dir is a dir
		printf("Not a directory passed as dir at inode %d with mode %d\n", parent->num, parent->mode & S_IFDIR);
		return NULL;
	}
	if(disk >= numdisks) { // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}
	struct wfs_dentry* curr_entry;

	// Checking for open spot in already allocated blocks
	for(int i = 0;i < N_BLOCKS;i++) {
		if(parent->blocks[i] != -1) {
			for(int j = 0;j < BLOCK_SIZE;j+= sizeof(struct wfs_dentry)) {
				curr_entry = (struct wfs_dentry*)((char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + parent->blocks[i] + j);
				if(curr_entry->num == 0) {
					return curr_entry;
				}
			}
		}
	}

	// Allocating a new block
	printf("Allocating new blcok for node %d\n", parent->num);
	for(int i = 0; i < N_BLOCKS;i++) {
		if(parent->blocks[i] == -1) {
			parent->blocks[i] = allocateBlock(disk);
			parent->size+= BLOCK_SIZE;
			curr_entry = (struct wfs_dentry*)((char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + parent->blocks[i]);
			return curr_entry;
		}
	}
	printf("Couldn't find space for dir nor could space be allocated\n");
	return NULL;
}

/** linkdir
* Adds a directory entry from parent to child and another from child to parent
**/ 
static int linkdir(struct wfs_inode* parent, struct wfs_inode* child, char* child_name, int disk) {
	struct wfs_dentry* parent_entry;
	//struct wfs_dentry* child_entry;

	parent_entry = findOpenDir(parent, disk);
	//child_entry = findOpenDir(child, disk);

	if(parent_entry == NULL) {
		printf("Parent or child entry not created\n");
		return -1;
	}

	// Enter into parent entry
	strncpy(parent_entry->name, child_name, MAX_NAME); // Copy child name into parent entry
	parent_entry->num = child->num;
	// Enter into child entry
	//child_entry->name[0] = '.';
	//child_entry->name[1] = '.';
	//child_entry->num = parent->num;

	//parent->nlinks++;
	child->nlinks = 1;

	return 0;
	
}

/** searchDir
* Returns the directory entry corresponding to the entry_name in the dir directory
**/
struct wfs_dentry* searchDir(struct wfs_inode* dir, char* entry_name, int disk) {
	if((dir->mode & S_IFDIR) == 0) { // Check if dir is a dir
		printf("Searching in not a directory %d\n", dir->num);
		return NULL;
	}
	if(disk >= numdisks) { // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}

	struct wfs_dentry* curr_entry;
	for(int i = 0; i < N_BLOCKS;i++) { // Iterate over blocks
		if(dir->blocks[i] != -1) { // Check if block is used
			for(int j =0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {

				// Go to data block offset and then add offset into block and then dirents
				curr_entry = (struct wfs_dentry*)((char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + dir->blocks[i] + j);
				if(curr_entry->name != 0 && strcmp(curr_entry->name, entry_name) == 0) { // If matching entry
					return curr_entry;
				}
			}
		}
	}
	printf("No entry found for %s in directory of inode %d\n", entry_name, dir->num);
	return NULL;
}

/** getInode
* Returns the inode at the end of the path
**/
struct wfs_inode* getInodePath(Path* path, int disk) {
	struct wfs_inode* current_inode;
	char* curr_entry_name;
	struct wfs_dentry* curr_entry_dirent;
	current_inode = roots[disk]; // Get root inode
	for(int i =0; i < path->size;i++) {
		curr_entry_name = path->path_components[i]; // Get next child name
		curr_entry_dirent  = searchDir(current_inode, curr_entry_name, disk);
		// Check if entry found
		if(curr_entry_dirent == NULL) {
			printf("Couldn't find entry %s\n", curr_entry_name);
			return NULL;
		}
		current_inode = getInode(curr_entry_dirent->num, disk);
	}

	return current_inode;
}

static int wfs_mkdir(const char* path, mode_t mode) {
	for(int disk = 0; disk < numdisks;disk++ ) {
		printf("wfs_mkdir\n");
		char* malleable_path;
		Path* p;
		char* dir_name;
		struct wfs_inode* parent;
		struct wfs_inode* child;

		// Making path modifiable
		malleable_path = strdup(path);
		if(malleable_path == NULL) {
			return -1;
		}
		
		p = splitPath(malleable_path); // Break apart path

		for(int i =0;i<p->size;i++) {
			printf("Path component [%d]: %s\n", i, p->path_components[i]);
		}

		// Checking if this file already exists
		if(getInodePath(p, disk) != NULL) {
			printf("File already exists\n");
			return -EEXIST;
		}
		
		// Strip last element but save name
		dir_name = p->path_components[p->size-1];
		p->size--;

		
		parent = getInodePath(p, disk);
		if(parent == NULL) {
			printf("Error getting parent\n");
		}
		
		child = allocateInode(disk);
		if(child == NULL) {
			printf("Error allocating child\n");
			return -ENOSPC;
		}
		
		child->mode |= mode;
		child->mode |= S_IFDIR;

		if(linkdir(parent,child, dir_name, disk) == -1) {
			printf("Linking error\n");
		}
	}

	
	return 0;
	
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			   off_t offset, struct fuse_file_info *fi)
{
	return 0;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
		for(int disk = 0; disk < numdisks;disk++ ) {
		printf("wfs_mknod\n");
		char* malleable_path;
		Path* p;
		char* dir_name;
		struct wfs_inode* parent;
		struct wfs_inode* child;

		// Making path modifiable
		malleable_path = strdup(path);
		if(malleable_path == NULL) {
			printf("couldnt get malleable path\n");
			return -1;
		}
		
		p = splitPath(malleable_path); // Break apart path

		for(int i =0;i<p->size;i++) {
			printf("Path component [%d]: %s\n", i, p->path_components[i]);
		}

		if(getInodePath(p, disk) != NULL) {
			printf("File already exists\n");
			return -EEXIST;
		}
		
		// Strip last element but save name
		dir_name = p->path_components[p->size-1];
		p->size--;

		
		parent = getInodePath(p, disk);
		if(parent == NULL) {
			printf("Error getting parent\n");
			return -1;
		}
		
		child = allocateInode(disk);
		if(child == NULL) {
			printf("Error allocating child\n");
			return -ENOSPC;
		}
		
		child->mode |= mode;

		if(linkdir(parent,child, dir_name, disk) == -1) {
			printf("Linking error\n");
		}
	}

	printf("mknod done\n");
	return 0;
}

static int wfs_unlink(const char *path)
{
	printf("wfs_unlink()\n");
	return 0;
}

static int wfs_rmdir(const char *path)
{
	printf("wfs_rmdir()\n");
	return 0;
}


static int wfs_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	printf("wfs_read\n");
	return 0;
}

static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	for(int disk =0; disk < numdisks;disk++) {
		
	
		printf("wfs_write()\n");
		int written_bytes = 0;
		Path* p;
		char* malleable_path;
		struct wfs_inode* my_file;
		off_t curr_block_offset;
		unsigned char* curr_block_ptr;
		int remaining_space;
		int curr_block_index;

		malleable_path = strdup(path);
		if(malleable_path == NULL) {
			printf("Couldnt create malleable_path\n");
			return -1;
		}

		p = splitPath(malleable_path);
		if(p == NULL) {
			printf("Couldnt split path\n");
			return -1;
		}

		my_file = getInodePath(p, disk);
		if(my_file == NULL) {
			printf("File does not exist\n");
			return -ENOENT;
		}

		curr_block_index = offset/BLOCK_SIZE;
		curr_block_offset = my_file->blocks[curr_block_index];

		if(curr_block_offset == -1) { // If block not allocated
			my_file->blocks[curr_block_index] = allocateBlock(disk); // try to allocate it
			curr_block_offset = my_file->blocks[curr_block_index];
			if(my_file->blocks[curr_block_index] == -1) { // If still not allocated then exit on error of no space
				printf("Cant allocate more file for write\n");
				return -ENOSPC;
			}
			my_file->size+=BLOCK_SIZE;
		}
		curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset + (offset%512);
		remaining_space = 512-(offset % 512);
		while(written_bytes != size) {

			// Ensuring block is allocated
			if(curr_block_offset == -1) {
				my_file->blocks[curr_block_index] = allocateBlock(disk);
				
				if(my_file->blocks[curr_block_index] == -1) { // If still not allocated then exit on error of no space
					printf("Cant allocate more file for write\n");
					return -ENOSPC;
				}
				curr_block_offset = my_file->blocks[curr_block_index];
				curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset;
				my_file->size+=BLOCK_SIZE;
			}
			
			// Check if we need to write larger than block space
			if(size - written_bytes >= remaining_space) {
				memcpy(curr_block_ptr, buf + written_bytes, remaining_space); // Fill rest of block
				written_bytes+=remaining_space; // Update how many bytes we have written

				// Go to next block
				curr_block_index++; 
				curr_block_offset = my_file->blocks[curr_block_index];
				curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset;
				remaining_space = BLOCK_SIZE;
				
			}

			// Write is less than remaining space in block
			else if(size - written_bytes < remaining_space){
				memcpy(curr_block_ptr, buf + written_bytes, size - written_bytes); // just write the bytes
				written_bytes += (size - written_bytes);
			}
			else {
				printf("Error you cant write more ... you shouldn't be here\n");
				return -1;
			}
			printf("Written bytes is %d\n", written_bytes);
		}

		// Free data
		free(malleable_path);
		
	}

	
	return 1;
}


static int wfs_getattr(const char* path, struct stat* stbuf)
{
	printf("wfs_getattr\n");
	printf("Path is %s\n", path);
	Path* p;
	struct wfs_inode* my_inode;
	char* malleable_path;
	malleable_path = strdup(path);
	
	if(malleable_path == NULL) {
		return -1;
	}
	printf("After dup\n");
	p = splitPath(malleable_path);
	printf("After split\n");
	if(p== NULL) {
		return -1;
	}

	printf("P->size is %d\n", p->size);
	for(int i =0; i < p->size;i++) {
		printf("p[%d] is %s\n", i, p->path_components[i]);
	}
	
	my_inode = getInodePath(p, 0);
	if(my_inode == NULL) {
		return -ENOENT;
	}
	
	stbuf->st_dev = 0;
	stbuf->st_ino = my_inode->num;
	stbuf->st_mode = my_inode->mode;
	stbuf->st_nlink = my_inode->nlinks;
	stbuf->st_uid = my_inode->uid;
	stbuf->st_gid = my_inode->gid;
	stbuf->st_rdev = 0;
	stbuf->st_size = my_inode->size;
	stbuf->st_blksize = BLOCK_SIZE;
	stbuf->st_blocks = my_inode->size / BLOCK_SIZE;
	printf("wfs_getattr done\n");


	for(int i =0;i < p->size;i++) {
			free(p->path_components[i]);
	}
	free(p);
	return 0;
}


void print_ibitmap(int disk){
	int numinodes = superblocks[0]->num_inodes;
	unsigned char* inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;
	for(int i = 0; i < numinodes/8; i++){
		 for (int j = 0; j < 8; j++) {
			 printf("%d", !!((*(inode_bitmap + i) << j) & 0x80));
		 }
		printf(" ");
	}
	printf("\n");
}
void print_dbitmap(int disk){
	int numdblocks = superblocks[disk]->num_data_blocks;
	unsigned char* data_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;
	for(int i = 0; i < numdblocks/8; i++){
		 for (int j = 0; j < 8; j++) {
			 printf("%d", !!((*(data_bitmap + i) << j) & 0x80));
		 }
		printf(" ");
	}
	printf("\n");
}

void wfs_destroy(void* private_data) {
	
	printf("wfs_destroy\n");
	struct wfs_inode* my_inode;
	for(int disk =0; disk < numdisks; disk++) {
		printf("DISK %d\n", disk);
		printf("Inode bitmap: ");
		print_ibitmap(disk);
		printf("Data bitmap: ");
		print_dbitmap(disk);
		my_inode = getInode(0, disk);
		printf("Inode [%d]: num = %d nlinks = %d\n", 0, my_inode->num, my_inode->nlinks);
		my_inode = getInode(1, disk);
		printf("Inode [%d]: num = %d nlinks = %d\n", 1, my_inode->num, my_inode->nlinks);
	}	
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
  .destroy = wfs_destroy,
};


unsigned char* bget(unsigned int bnum, int disk) {

	unsigned char* ret_val;
	// Checking if bitmap is allocated
	if(checkDBitmap(bnum,disk) == 0) {
		printf("Data Block is not allocated\n");
		return NULL;
	}
	
	// Go to data offset
	ret_val = mappings[disk] + superblocks[disk]->d_blocks_ptr + (bnum * BLOCK_SIZE);
	return ret_val;
}

int mapDisks(int argc, char* argv[]) {
	int i = 1;

	// Reading disk images
	while(i < argc && argv[i][0] != '-'){ 

		//read in the disks
		numdisks++;
		printf("argv[%d]: %s\n", i, argv[i]);	
		disks = realloc(disks, sizeof(int) * numdisks);
		int fd = open(argv[i], O_RDWR);
	
		if(fd == -1){	
			printf("mapDisks(): failed to open file\n");
			//free(argv[i]);
			exit(1);
		}
		disks[i-1] = fd;
		i++;
	}

	// Allocating array to hold the size of the disks
	disk_size = malloc(sizeof(int) * numdisks);
	if(disk_size == NULL) {
		printf("Failed to allocate arr for disk sizes\n");
		exit(1);
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
		mappings[k] = mmap(NULL, my_stat.st_size, PROT_READ|PROT_WRITE, MAP_SHARED ,disks[k], 0); // Map this image into mem
		disk_size[k] = my_stat.st_size;
		// Check if mmap worked
		if(mappings[k] == MAP_FAILED) {
			printf("Error, couldn't mmap disk into memory\n");
			exit(1);
		}
		
		// Set superblock and root according to offsets
		superblocks[k] = (struct wfs_sb*)mappings[k];
		roots[k] = (struct wfs_inode*)((char*)superblocks[k] + superblocks[k]->i_blocks_ptr);
	}
	printf("end mapdisks\n");
	return i;

}

int my_tests() {
	char path[] = "hello/world/abby/armstrong/harley";
	Path* p;
	p = splitPath(path);
	printf("p[0] is %s\n", p->path_components[0]);
	return 0;
}


int main(int argc, char* argv[])
{
	//FOR VALGRIND
	//argc = argc-1;
	//argv = &argv[1];
	
	int new_argc; // Used to pass into fuse_main

	// TODO: INITIALIZE Raid_mode
	raid_mode = 1;
	mapDisks(argc, argv);
	
	new_argc = (argc - numdisks); // Gets difference of what was already read vs what isnt 
	char* new_argv[new_argc];
	new_argv[0] = "./wfs";
	for(int j = 1;j < new_argc; j++) {
		new_argv[j] = argv[numdisks + j];
	}

	for(int i =0;i < new_argc;i++) {
		printf("New arg [%d]: %s\n", i, new_argv[i]);
	}

	printf("Num disks %d\n",numdisks);
	for(int i =0;i < numdisks;i++) {
		printf("Disk [%d]: %d\n", i, disk_size[i]);
	}

		
	return fuse_main(new_argc, new_argv, &ops, NULL);	

}
