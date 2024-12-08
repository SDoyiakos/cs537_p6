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
static int *disks;
static int *disk_size;
static unsigned char **mappings;
static int numdisks = 0;
static struct wfs_sb **superblocks;
static struct wfs_inode **roots;
static int next_disk = 0;

struct PathListNode
{
	char *data;
	struct PathListNode *next;
};

typedef struct
{
	char **path_components;
	int size;
} Path;

struct IndirectBlock
{
	off_t blocks[BLOCK_SIZE / sizeof(off_t)];
};

// ------------HELPTER FUNCINTS-----------------
// entry is the encoded values. This returns the block offset;
static int getEntryOffset(int entry) {
	int ret_val = entry;
	entry = entry % BLOCK_SIZE;
	ret_val-= entry;
	return ret_val;
}

// returns teh disk for  given entry
static int getEntryDisk(int entry) {
	int ret_val = entry % BLOCK_SIZE;
	return ret_val;
}

// tshi returnst eh next disk and updates it
static int getNextDisk() {
	int ret_val = next_disk;
	next_disk= (next_disk + 1) % numdisks;
	return ret_val;
}
static int checkDBitmap(unsigned int inum, int disk)
{
	int byte_dist = inum / 8;		 // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char *data_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;
	unsigned char bit_val;

	data_bitmap += byte_dist; // Go byte_dist bytes over
	bit_val = *data_bitmap;
	bit_val &= (1 << offset); // Shift over offset times
	if (bit_val > 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static int checkIBitmap(unsigned int inum, int disk)
{

	int byte_dist = inum / 8;		 // how many byes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char *inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;
	unsigned char bit_val;

	inode_bitmap += byte_dist; // Go byte_dist bytes over
	bit_val = *inode_bitmap;
	bit_val &= (1 << offset); // Shift over offset times
	if (bit_val > 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static int markbitmap_d(unsigned int bnum, int used, int disk)
{

	int byte_dist = bnum / 8;		 // how many byes away from start bnum is
	unsigned char offset = bnum % 8; // We want to start at lower bits
	unsigned char *blocks_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;

	blocks_bitmap += byte_dist; // Go byte_dist bytes over
	// mark it 0
	if (used != 1)
	{
		unsigned char mask = 1;
		mask = mask << offset;
		mask = ~mask;
		*blocks_bitmap &= mask;
		return 0;
	}
	// mark it one
	*blocks_bitmap = *blocks_bitmap | used << offset;

	return 0;
}

static int markbitmap_i(unsigned int inum, int used, int disk)
{

	int byte_dist = inum / 8;		 // how many bytes away from start inum is
	unsigned char offset = inum % 8; // We want to start at lower bits
	unsigned char *inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;

	inode_bitmap += byte_dist; // Go byte_dist bytes over
	if (used != 1)
	{
		unsigned char mask = 1;
		mask = mask << offset;
		mask = ~mask;
		*inode_bitmap &= mask;
		return 0;
	}
	*inode_bitmap = *inode_bitmap | (unsigned char)used << offset;
	return 0;
}

static int findFreeInode(int disk)
{

	// Iterate over all entries until one that isnt mapped is found
	for (int i = 0; i < (int)superblocks[disk]->num_inodes; i++)
	{
		if (checkIBitmap(i, disk) == 0)
		{
			return i;
		}
	}
	return -1; // Return -1 if no open mappings are found
}

static int findFreeData(int disk)
{

	// Iterate over all entries until one that isnt mapped is found
	for (int i = 0; i < (int)superblocks[disk]->num_data_blocks; i++)
	{
		if (checkDBitmap(i, disk) == 0)
		{
			return i;
		}
	}
	return -1; // Return -1 if no open mappings are found
}

/** allocateBlock
 * Finds an open block on the given disk and then returns its offset
 **/
static int allocateBlock(int disk)
{
	int ret_val;
	int data_bit;

	// Find open spot
	data_bit = findFreeData(disk);
	if (data_bit == -1)
	{
		return -1;
	}

	ret_val = BLOCK_SIZE * data_bit; // Offset is 512 * data_bit

	// Initialize new block to zero
	memset((unsigned char *)mappings[disk] + superblocks[disk]->d_blocks_ptr + ret_val, 0, BLOCK_SIZE);

	markbitmap_d(data_bit, 1, disk); // Mark this as allocated
	if(raid_mode == 0) {
		ret_val +=disk;
	}
	return ret_val;					 // Returns first entry within block
}

// initializeIndirectBlock
// allocates a block for the indirect block and initialies all its pointers to -1
static int initializeIndirectBlock(int disk){
	// allocate the first block
	printf("initalizeIndirectBlock()\n");
	int indirectBlock_offset = allocateBlock(disk);
	struct IndirectBlock *indirectBlock	= (struct IndirectBlock *)(mappings[disk] + superblocks[disk]->d_blocks_ptr + indirectBlock_offset);
	for(int i = 0; i < (BLOCK_SIZE / sizeof(off_t)); i++){
		indirectBlock->blocks[i] = -1;
	}
	return indirectBlock_offset;

}


/** allocateInode
 * Finds an open inode and then returns its offset from inode ptr
 **/
static struct wfs_inode *allocateInode(int disk)
{
	int ret_val;
	int data_bit;

	data_bit = findFreeInode(disk);
	if (data_bit == -1)
	{
		return NULL;
	}

	ret_val = BLOCK_SIZE * data_bit;
	markbitmap_i(data_bit, 1, disk);

	// Initialize inode
	struct wfs_inode *my_inode;
	my_inode = (struct wfs_inode *)((char *)mappings[disk] + superblocks[disk]->i_blocks_ptr + ret_val);
	my_inode->mode = 0x777; // RW for UGO
	my_inode->num = data_bit;
	my_inode->uid = getuid();
	my_inode->gid = getgid();
	my_inode->size = 0; 
	my_inode->nlinks = 0;
	my_inode->atim = time(0);
	my_inode->mtim = time(0);
	my_inode->ctim = time(0);
	for (int i = 0; i < N_BLOCKS; i++)
	{
		my_inode->blocks[i] = -1;
	}
	return my_inode;
}

/** splitPath
 * Returns a Path struct which will contain an array of each entry in the path
 **/
static Path *splitPath(char *path)
{
	Path *ret_path;
	char *split_val;

	// Allocate the path
	ret_path = malloc(sizeof(Path));
	if (ret_path == NULL)
	{
		printf("Couldn't allocate path struct\n");
	}
	ret_path->size = 0;

	split_val = strtok(path, "/");

	while (split_val != NULL)
	{

		// If size = 0
		if (ret_path->size == 0)
		{
			ret_path->path_components = malloc(sizeof(char *));
			if (ret_path == NULL)
			{
				printf("Couldn't allocate path arr\n");
				return NULL;
			}
		}

		// If size > 0
		else
		{
			ret_path->path_components = realloc(ret_path->path_components, sizeof(char *) * (ret_path->size + 1));
			if (ret_path->path_components == NULL)
			{
				printf("Error, realloc of path failed\n");
				return NULL;
			}
		}

		// Allocate entry
		ret_path->path_components[ret_path->size] = strdup(split_val);
		if (ret_path->path_components[ret_path->size] == NULL)
		{
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
static struct wfs_inode *getInode(int inum, int disk)
{
	// Check if its allocated
	if (checkIBitmap(inum, disk) == 0)
	{
		printf("Inode isn't allocated\n");
		return NULL;
	}
	return (struct wfs_inode *)((char *)mappings[disk] + superblocks[disk]->i_blocks_ptr + (BLOCK_SIZE * inum));
}
/** findOpenDir
 * Finds an open directory in the parent directory
 **/
static struct wfs_dentry *findOpenDir1(struct wfs_inode *parent, int disk)
{
	if ((parent->mode & S_IFDIR) == 0)
	{ // Check if dir is a dir
		printf("Not a directory passed as dir at inode %d with mode %d\n", parent->num, parent->mode & S_IFDIR);
		return NULL;
	}
	if (disk >= numdisks)
	{ // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}
	struct wfs_dentry *curr_entry;

	// Checking for open spot in already allocated blocks
	for (int i = 0; i < N_BLOCKS; i++)
	{
		if (parent->blocks[i] != -1)
		{
			for (int j = 0; j < BLOCK_SIZE; j += sizeof(struct wfs_dentry))
			{
				curr_entry = (struct wfs_dentry *)((char *)mappings[disk] + superblocks[disk]->d_blocks_ptr + parent->blocks[i] + j);
				if (curr_entry->num == 0)
				{
					return curr_entry;
				}
			}
		}
	}

	// Allocating a new block
	printf("Allocating new blcok for node %d\n", parent->num);
	for (int i = 0; i < N_BLOCKS; i++)
	{
		if (parent->blocks[i] == -1)
		{
			parent->blocks[i] = allocateBlock(disk);
			curr_entry = (struct wfs_dentry*)((char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + parent->blocks[i]);
			return curr_entry;
		}
	}
	printf("Couldn't find space for dir nor could space be allocated\n");
	return NULL;
}

/** findOpenDir
 * Finds an open directory in the parent directory
 **/
static struct wfs_dentry *findOpenDir0(struct wfs_inode *parent, int disk)
{
	if ((parent->mode & S_IFDIR) == 0)
	{ // Check if dir is a dir
		printf("Not a directory passed as dir at inode %d with mode %d\n", parent->num, parent->mode & S_IFDIR);
		return NULL;
	}
	if (disk >= numdisks)
	{ // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}
	struct wfs_dentry *curr_entry;

	// Checking for open spot in already allocated blocks
	for (int i = 0; i < N_BLOCKS; i++)
	{
		if (parent->blocks[i] != -1)
		{
			for (int j = 0; j < BLOCK_SIZE; j += sizeof(struct wfs_dentry))
			{	
				disk = getEntryDisk(parent->blocks[i]);
				curr_entry = (struct wfs_dentry *)((char *)mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(parent->blocks[i]) + j);
				if (curr_entry->num == 0)
				{
					return curr_entry;
				}
			}
		}
	}

	// Allocating a new block
	printf("Allocating new blcok for node %d\n", parent->num);
	for (int i = 0; i < N_BLOCKS; i++)
	{
		if (parent->blocks[i] == -1)
		{
			disk = getNextDisk(); // We're going to write to a new disk
			parent->blocks[i] = allocateBlock(disk); // Allocate a block on the new disk
			curr_entry = (struct wfs_dentry*)((char*)mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(parent->blocks[i]));
			printf("Allocated block on disk %d\n", disk);
			return curr_entry;
		}
	}
	printf("Couldn't find space for dir nor could space be allocated\n");
	return NULL;
}

/** findOpenDir
 * Finds an open directory in the parent directory
 **/
static struct wfs_dentry *findOpenDir(struct wfs_inode *parent, int disk)
{
	if(raid_mode == 0) {
		printf("Find open dir 0\n");
		return findOpenDir0(parent, disk);
	}
	else if(raid_mode == 1) {
		printf("Find open dir 1\n");
		return findOpenDir1(parent, disk);
	}
	return NULL;
}

/** linkdir
 * Adds a directory entry from parent to child and another from child to parent
 **/
static int linkdir(struct wfs_inode *parent, struct wfs_inode *child, char *child_name, int disk)
{
	struct wfs_dentry *parent_entry;
	// struct wfs_dentry* child_entry;

	parent_entry = findOpenDir(parent, disk);
	// child_entry = findOpenDir(child, disk);

	if (parent_entry == NULL)
	{
		printf("Parent or child entry not created\n");
		return -1;
	}

	// Enter into parent entry
	strncpy(parent_entry->name, child_name, MAX_NAME); // Copy child name into parent entry
	parent_entry->num = child->num;
	// Enter into child entry
	// child_entry->name[0] = '.';
	// child_entry->name[1] = '.';
	// child_entry->num = parent->num;

	// parent->nlinks++;
	child->nlinks = 1;
	parent->size += sizeof(struct wfs_dentry);

	return 0;
}
/** deleteDentry
 * removes a directory entry
 **/
static int deleteDentry(struct wfs_inode *dir, char *entry_name, int disk)
{
	printf("deleteDentry(), dir->num: %d entry_name: %s disk: %d\n",dir->num, entry_name, disk );
	if ((dir->mode & S_IFDIR) == 0)
	{ // Check if dir is a dir
		printf("deleteDentry() not a directory %d\n", dir->num);
		return -1;
	}
	if (disk >= numdisks)
	{ // Check if this is a valid disk
		printf("Not a valid disk\n");
		return -1;
	}

	struct wfs_dentry *curr_entry;
	for (int i = 0; i < N_BLOCKS; i++)
	{ // Iterate over blocks
		if (dir->blocks[i] != -1)
		{ // Check if block is used
			for (uint j = 0; j < BLOCK_SIZE; j += sizeof(struct wfs_dentry))
			{

				// Go to data block offset and then add offset into block and then dirents
				curr_entry = (struct wfs_dentry *)(mappings[disk]
					 + superblocks[disk]->d_blocks_ptr 
					+ dir->blocks[i] + j);

				if(curr_entry->num != 0){
					printf("curr_entry->name: %s\n", curr_entry->name);
				}
				if (curr_entry->num != 0 && strcmp(curr_entry->name, entry_name) == 0)
				{ // If matching entry
					memset((void *)curr_entry, 0, sizeof(struct wfs_dentry));
					return 0;
				}
			}
		}
	}
	printf("No entry found for %s in directory of inode %d\n", entry_name, dir->num);
	return -1;
}

/** searchDir
 * Returns the directory entry corresponding to the entry_name in the dir directory
 **/
static struct wfs_dentry *searchDir0(struct wfs_inode *dir, char *entry_name, int disk)
{
	if ((dir->mode & S_IFDIR) == 0)
	{ // Check if dir is a dir
		printf("Searching in not a directory %d\n", dir->num);
		return NULL;
	}
	if (disk >= numdisks)
	{ // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}

	struct wfs_dentry *curr_entry;

	for (int i = 0; i < N_BLOCKS; i++)
	{ // Iterate over blocks
		if (dir->blocks[i] != -1)
		{ // Check if block is used
			for (int j = 0; j < BLOCK_SIZE; j += sizeof(struct wfs_dentry))
			{
				// Go to data block offset and then add offset into block and then dirents
				disk = getEntryDisk(dir->blocks[i]);
				curr_entry = (struct wfs_dentry *)((char *)mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(dir->blocks[i]) + j);
				if (curr_entry->name != 0 && strcmp(curr_entry->name, entry_name) == 0)
				{ // If matching entry
					return curr_entry;
				}
			}
		}
	}
	printf("No entry found for %s in directory of inode %d\n", entry_name, dir->num);
	return NULL;
}
/** searchDir
 * Returns the directory entry corresponding to the entry_name in the dir directory
 **/
static struct wfs_dentry *searchDir1(struct wfs_inode *dir, char *entry_name, int disk)
{
	if ((dir->mode & S_IFDIR) == 0)
	{ // Check if dir is a dir
		printf("Searching in not a directory %d\n", dir->num);
		return NULL;
	}
	if (disk >= numdisks)
	{ // Check if this is a valid disk
		printf("Not a valid disk\n");
		return NULL;
	}

	struct wfs_dentry *curr_entry;
	for (int i = 0; i < N_BLOCKS; i++)
	{ // Iterate over blocks
		if (dir->blocks[i] != -1)
		{ // Check if block is used
			for (int j = 0; j < BLOCK_SIZE; j += sizeof(struct wfs_dentry))
			{

				// Go to data block offset and then add offset into block and then dirents
				curr_entry = (struct wfs_dentry *)((char *)mappings[disk] + superblocks[disk]->d_blocks_ptr + dir->blocks[i] + j);
				if (curr_entry->name != 0 && strcmp(curr_entry->name, entry_name) == 0)
				{ // If matching entry
					return curr_entry;
				}
			}
		}
	}
	printf("No entry found for %s in directory of inode %d\n", entry_name, dir->num);
	return NULL;
}


/** searchDir
 * Returns the directory entry corresponding to the entry_name in the dir directory
 **/
static struct wfs_dentry *searchDir(struct wfs_inode *dir, char *entry_name, int disk)
{
	if(raid_mode == 1) {
		return searchDir1(dir, entry_name, disk);
	}
	else if(raid_mode == 0) {
		return searchDir0(dir, entry_name, disk);
	}
	return NULL;
}

/** getInode
 * Returns the inode at the end of the path
 **/
static struct wfs_inode *getInodePath1(Path *path, int disk)
{
	printf("getInodePath path: \n"); 
	struct wfs_inode *current_inode;
	char *curr_entry_name;
	struct wfs_dentry *curr_entry_dirent;
	current_inode = roots[disk]; // Get root inode 
	for (int i = 0; i < path->size; i++)
	{
		curr_entry_name = path->path_components[i]; // Get next child name
		curr_entry_dirent = searchDir(current_inode, curr_entry_name, disk);
		// Check if entry found
		if (curr_entry_dirent == NULL)
		{
			printf("Couldn't find entry %s\n", curr_entry_name);
			return NULL;
		}
		current_inode = getInode(curr_entry_dirent->num, disk);
	}

	return current_inode;
}
/** getInode
 * Returns the inode at the end of the path
 **/
static struct wfs_inode *getInodePath0(Path *path, int disk)
{
	printf("getInodePath0\n"); 
	struct wfs_inode *current_inode;
	char *curr_entry_name;
	struct wfs_dentry *curr_entry_dirent;
	current_inode = roots[disk]; // Get root inode 
	for (int i = 0; i < path->size; i++)
	{
		curr_entry_name = path->path_components[i]; // Get next child name
		curr_entry_dirent = searchDir(current_inode, curr_entry_name, disk);
		// Check if entry found
		if (curr_entry_dirent == NULL)
		{
			printf("Couldn't find entry %s\n", curr_entry_name);
			return NULL;
		}
		current_inode = getInode(curr_entry_dirent->num, disk);
	}

	return current_inode;
}

/** getInode
 * Returns the inode at the end of the path
 **/
static struct wfs_inode *getInodePath(Path *path, int disk)
{
	return getInodePath1(path, disk);
}

// finds entry at the de_offset, then finds offset to next dentry for raid0
// start_de_offset still == offset from data_blocks_ptr to next direntry
static struct wfs_dentry *findNextDir0(struct wfs_inode *directory, off_t start_de_offset, off_t *new_de_offset, int disk){

	printf("-------------------findNextDir0()--------------\n");
	printf("Dir num: %d\nStart offset %d\nDisk: %d\n", directory->num, start_de_offset, disk);
	struct wfs_dentry *current_de = (struct wfs_dentry *)(mappings[disk] + superblocks[disk]->d_blocks_ptr + start_de_offset);
	struct wfs_dentry *next_de;

	int start_block = start_de_offset / BLOCK_SIZE;

	// if its the first time calling findNextDir, then get the first de in the dir
	if (start_de_offset == 0)
	{
		printf("de_offset == 0\n");
		int found = 0;

		for (int b = 0; b < N_BLOCKS; b++)
		{
			if (found != 0)
			{
				break;
			}

			if (directory->blocks[b] == -1)
			{
				continue;
			}

			for (int i = 0; i < BLOCK_SIZE; i += sizeof(struct wfs_dentry))
			{

				current_de = (struct wfs_dentry *)(mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(directory->blocks[b]) + i);
				printf("Current de is %d\n", current_de);
				if (current_de->num != 0)
				{
					start_de_offset = getEntryOffset(directory->blocks[b]) + i;
					found = 1;
					start_block = b;
					break;
				}
			}
		}

		// IF DIR IS EMPTY
		if (found == 0)
		{
			return NULL;
		}
	}

	printf("start_block: %d, start_de_offset: %ld \n", start_block, start_de_offset);
	uint min_offset = start_de_offset;
	// NOW that we have de_offset and current_de, find the next_de's offset
	// if de_offset is the end of a block, start searching for the next de at the next block. 
	// otherwise, search for the next de in the current block. If the de_offset
	for (int b = start_block; b < N_BLOCKS; b++)
	{
		printf("b: %d\n", b);
		if(directory->blocks[b] == -1){
			continue;
		}
		
		uint o;
		for (o = ((min_offset % BLOCK_SIZE)); o < BLOCK_SIZE;
			 o += sizeof(struct wfs_dentry))
		{
			printf("we here: o: %d\n", o);
			next_de = (struct wfs_dentry *)(mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(directory->blocks[b]) +  o);

			if ((next_de->num != 0) && (strcmp(next_de->name, current_de->name) != 0))
			{
				printf("FOUND NEXT DIR: curr_dir->name: %s next_de->name : %s\n", current_de->name, next_de->name);
				*new_de_offset = getEntryOffset(directory->blocks[b]) + o;
				return current_de;
			}
		}
		
		// we need to go to the next block
		min_offset = 0;
	}
	
	// Reached end of directory and no new dentries found
	printf("end of dir\n");
	*new_de_offset = 0;
	return current_de;
}

// finds the dentry at the de_offset, then finds the offset to the next direntry. Returns
// eg: let mnt have files a b c.
// not
// findNextDir(root, 0, new_offset) = a, new_offset = offset to b.
// findNextDir(root, 12, new_off) = b, new_off = offset to c.
// findNextDir(root, c_ffset, new_off) = c, new_off = 0
// note that blocks[b] == offset from d_blocks_ptr
static struct wfs_dentry *findNextDir1(struct wfs_inode *directory, off_t de_offset, off_t *new_de_offset)
{

	printf("-------------------findNextDir()--------------\n");
	struct wfs_dentry *current_de = (struct wfs_dentry *)(mappings[0] + superblocks[0]->d_blocks_ptr + de_offset);
	struct wfs_dentry *next_de;

	int start_block = de_offset / BLOCK_SIZE;

	// if its the first time calling findNextDir, then get the first de in the dir
	if (de_offset == 0)
	{
		printf("de_offset == 0\n");
		int found = 0;

		for (int b = 0; b < N_BLOCKS; b++)
		{
			if (found != 0)
			{
				break;
			}

			if (directory->blocks[b] == -1)
			{
				continue;
			}

			for (int i = 0; i < BLOCK_SIZE; i += sizeof(struct wfs_dentry))
			{

				current_de = (struct wfs_dentry *)(mappings[0] + superblocks[0]->d_blocks_ptr + directory->blocks[b] + i);

				if (current_de->num != 0)
				{
					de_offset = directory->blocks[b] + i;
					found = 1;
					start_block = b;
					break;
				}
			}
		}

		// IF DIR IS EMPTY
		if (found == 0)
		{
			return NULL;
		}
	}

	printf("start_block: %d, de_offset: %ld \n", start_block, de_offset);
	uint min_offset = de_offset;
	// NOW that we have de_offset and current_de, find the next_de's offset
	// if de_offset is the end of a block, start searching for the next de at the next block. 
	// otherwise, search for the next de in the current block. If the de_offset
	for (int b = start_block; b < N_BLOCKS; b++)
	{
		printf("b: %d\n", b);
		if(directory->blocks[b] == -1){
			continue;
		}
		
		uint o;
		for (o = ((min_offset % BLOCK_SIZE)); o < BLOCK_SIZE;
			 o += sizeof(struct wfs_dentry))
		{
			printf("we here: o: %d\n", o);
			next_de = (struct wfs_dentry *)(mappings[0] + superblocks[0]->d_blocks_ptr + directory->blocks[b] +  o);

			if ((next_de->num != 0) && (strcmp(next_de->name, current_de->name) != 0))
			{
				printf("FOUND NEXT DIR: curr_dir->name: %s next_de->name : %s\n", current_de->name, next_de->name);
				*new_de_offset = directory->blocks[b] + o;
				return current_de;
			}
		}
		
		// we need to go to the next block
		min_offset = 0;
	}
	
	// Reached end of directory and no new dentries found
	printf("end of dir\n");
	*new_de_offset = 0;
	return current_de;
}

void print_ibitmap(int disk)
{
	int numinodes = superblocks[disk]->num_inodes;
	unsigned char *inode_bitmap = mappings[disk] + superblocks[disk]->i_bitmap_ptr;
	for (int i = 0; i < numinodes / 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			printf("%d", !!((*(inode_bitmap + i) << j) & 0x80));
		}
		printf(" ");
	}
	printf("\n");
}
void print_dbitmap(int disk)
{
	int numdblocks = superblocks[disk]->num_data_blocks;
	unsigned char *data_bitmap = mappings[disk] + superblocks[disk]->d_bitmap_ptr;
	for (int i = 0; i < numdblocks / 8; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			printf("%d", !!((*(data_bitmap + i) << j) & 0x80));
		}
		printf(" ");
	}
	printf("\n");
}

void wfs_destroy(void *private_data)
{
	printf("wfs_destroy\n");
}

unsigned char *bget(unsigned int bnum, int disk)
{

	unsigned char *ret_val;
	// Checking if bitmap is allocated
	if (checkDBitmap(bnum, disk) == 0)
	{
		printf("Data Block is not allocated\n");
		return NULL;
	}

	// Go to data offset
	ret_val = mappings[disk] + superblocks[disk]->d_blocks_ptr + (bnum * BLOCK_SIZE);
	return ret_val;
}

int mapDisks(int argc, char *argv[])
{
	int i = 1;

	// Reading disk images
	while (i < argc && argv[i][0] != '-')
	{

		// read in the disks
		numdisks++;
		printf("argv[%d]: %s\n", i, argv[i]);
		disks = realloc(disks, sizeof(int) * numdisks);
		int fd = open(argv[i], O_RDWR);
		if (fd == -1)
		{
			printf("mapDisks(): failed to open file\n");
			// free(argv[i]);
			exit(1);
		}
		disks[i - 1] = fd;
		i++;
	}

	// Allocating array to hold the size of the disks
	disk_size = malloc(sizeof(int) * numdisks);
	if (disk_size == NULL)
	{
		printf("Failed to allocate arr for disk sizes\n");
		exit(1);
	}

	// Allocate region for beginning ptr in mappings
	mappings = malloc(sizeof(void *) * numdisks);
	if (mappings == NULL)
	{
		printf("Failed to allocate mapping addrs\n");
		exit(1);
	}

	// Allocate region in mem for superblock pointers
	superblocks = malloc(sizeof(struct wfs_sb *) * numdisks);
	if (superblocks == NULL)
	{
		printf("Failed to allocate superblocks\n");
		exit(1);
	}

	// Allocate region in mem for root of each image
	roots = malloc(sizeof(struct wfs_inode *) * numdisks);
	if (roots == NULL)
	{
		printf("Unable to allocate roots\n");
		exit(1);
	}

	// Map every disk into memory
	struct stat my_stat;
	int disk_order;
	struct wfs_sb disk_superblock;

	for (int k = 0; k < numdisks; k++)
	{
		read(disks[k], &disk_superblock, sizeof(struct wfs_sb));
		disk_order = disk_superblock.disk_order -1; // We start at order 1 so subtract 1
		fstat(disks[k], &my_stat);																	// Get file information about disk image
		mappings[disk_order] = mmap(NULL, my_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, disks[k], 0); // Map this image into mem
		disk_size[disk_order] = my_stat.st_size;
		// Check if mmap worked
		if (mappings[disk_order] == MAP_FAILED)
		{
			printf("Error, couldn't mmap disk into memory\n");
			exit(1);
		}

		// Set superblock and root according to offsets
		superblocks[disk_order] = (struct wfs_sb *)mappings[disk_order];
		roots[disk_order] = (struct wfs_inode *)((char *)superblocks[disk_order] + superblocks[disk_order]->i_blocks_ptr);
	}

	// Check disk order
	for(int j =0;j<numdisks;j++) {
		if(superblocks[j]->total_disks != numdisks) {
			printf("Discrepancy between total disk count and superblock value\n");
			exit(-1);
		}
		if(superblocks[j]->disk_order != j+1) {
			printf("Error disks out of order, order %d, expected %d\n", superblocks[j]->disk_order, j+1);
			//exit(-1);
		}
	}
	raid_mode = superblocks[0]->raid_mode;
	printf("end mapdisks\n");
	return i;
}

//----------------------CALLBACL FCNS----------------------


static int wfs_mkdir0(const char *path, mode_t mode)
{
		printf("wfs_mkdir\n");
		char *malleable_path;
		Path *p;
		char *dir_name;
		struct wfs_inode *parent;
		struct wfs_inode *child;

		// Making path modifiable
		malleable_path = strdup(path);
		if (malleable_path == NULL)
		{
			return -1;
		}

		p = splitPath(malleable_path); // Break apart path

		for (int i = 0; i < p->size; i++)
		{
			printf("Path component [%d]: %s\n", i, p->path_components[i]);
		}

		// Checking if this file already exists
		if (getInodePath(p, 0) != NULL)
		{
			printf("File already exists\n");
			return -EEXIST;
		}

		// Strip last element but save name
		dir_name = p->path_components[p->size - 1];
		p->size--;

		parent = getInodePath(p, 0);
		if (parent == NULL)
		{
			printf("Error getting parent\n");
		}

		child = allocateInode(0);
		if (child == NULL)
		{
			printf("Error allocating child\n");
			return -ENOSPC;
		}

		child->mode |= mode;
		child->mode |= S_IFDIR;

		if (linkdir(parent, child, dir_name, 0) == -1)
		{
			printf("Linking error\n");
		}

		struct wfs_inode* first; 
		struct wfs_inode* second;
		for(int k = 1; k < numdisks;k++) {
		first = (struct wfs_inode*)(mappings[0] + superblocks[0]->i_blocks_ptr);
		second = (struct wfs_inode*)(mappings[k] + superblocks[k]->i_blocks_ptr);
		for(int i = 0; i < superblocks[0]->num_inodes;i++){
			memcpy(second, first, sizeof(struct wfs_inode));
			printf("First num is %d\nSecond num is %d\n", first->num, second->num);
			first = (struct wfs_inode*)((char*)first + BLOCK_SIZE);
			second = (struct wfs_inode*)((char*)second + BLOCK_SIZE);
		}

		// Copying inode bitmaps
		memcpy(superblocks[k], superblocks[0], superblocks[0]->d_bitmap_ptr);
		
	}
	
	return 0;
}

static int wfs_mkdir1(const char *path, mode_t mode)
{
	for (int disk = 0; disk < numdisks; disk++)
	{
		printf("wfs_mkdir\n");
		char *malleable_path;
		Path *p;
		char *dir_name;
		struct wfs_inode *parent;
		struct wfs_inode *child;

		// Making path modifiable
		malleable_path = strdup(path);
		if (malleable_path == NULL)
		{
			return -1;
		}

		p = splitPath(malleable_path); // Break apart path

		for (int i = 0; i < p->size; i++)
		{
			printf("Path component [%d]: %s\n", i, p->path_components[i]);
		}

		// Checking if this file already exists
		if (getInodePath(p, disk) != NULL)
		{
			printf("File already exists\n");
			return -EEXIST;
		}

		// Strip last element but save name
		dir_name = p->path_components[p->size - 1];
		p->size--;

		parent = getInodePath(p, disk);
		if (parent == NULL)
		{
			printf("Error getting parent\n");
		}

		child = allocateInode(disk);
		if (child == NULL)
		{
			printf("Error allocating child\n");
			return -ENOSPC;
		}

		child->mode |= mode;
		child->mode |= S_IFDIR;

		if (linkdir(parent, child, dir_name, disk) == -1)
		{
			printf("Linking error\n");
		}
	}
	return 0;
}

static int wfs_mkdir(const char *path, mode_t mode)
{
	if(raid_mode == 0) {
		return wfs_mkdir0(path, mode);
	}
	else if(raid_mode == 1) {
		return wfs_mkdir1(path, mode);
	}
	return -1;
}
// Remove (delete) the given file, symbolic link, hard link, or special node.
//  Note that if you support hard links, unlink only deletes the data when the last hard link is removed.
//  See unlink(2) for details.
//  To delete files, you should free (unallocate) any data blocks associated with the file, free it's inode,
// and remove the directory entry pointing to the file from the parent inode.

static int wfs_unlink(const char *path)
{
	printf("unlink(): path: %s\n",  path);
	// get the dir and file inode
	struct wfs_inode *directory;
	struct wfs_inode *file;
	char *file_name;
	// RAID 1:

	for (int disk = 0; disk < numdisks; disk++)
	{
		char *pathcpy = strdup(path);
		if (pathcpy == NULL)
		{
			printf("fialed strdup unlink\n");
			return -1;
		}
		Path *splitpath = splitPath(pathcpy);
		file_name = splitpath->path_components[splitpath->size-1];

		if ((file = getInodePath(splitpath, disk)) == NULL)
		{
			printf("File doesnt exists\n");
			return -ENOENT;
		}

		
		if(splitpath->size > 1){
			splitpath->size--;
			directory = getInodePath(splitpath, disk);
		} else {
			// IF ROOT
			directory = roots[disk];
		}

		if (directory == NULL)
		{
			printf("Error getting directory\n");
			return -ENOENT;
		}

		if (deleteDentry(directory, file_name, disk) != 0)
		{
			printf("failed to remove file's dentry from dir\n");
			return -1;
		}

		// DELETE FILE IF NLINKS== 0
		file->nlinks--;
		if (file->nlinks == 0)
		{
			// TODO: UNCLEAR WHETHER WE MUST ZERO THESE OUT
			//  free the direct blocks
			printf("am deleting file\n");
			int block_num;
			for (int d = 0; d < N_BLOCKS -1; d++)
			{
				if(file->blocks[d] == -1){
					continue;
				}
				block_num = file->blocks[d] / BLOCK_SIZE;
				printf("direct blockk num: %d\n", block_num);
				void *block = mappings[disk] + superblocks[disk]->d_blocks_ptr + file->blocks[d];
				if (memset(block, 0, BLOCK_SIZE) != block)
				{
					printf("unlink(): memset failed\n");
					return -1;
				}
		
				markbitmap_d(block_num, 0, disk);
			}

			// free the indirect blocks
			if (file->blocks[IND_BLOCK] != -1)
			{
				struct IndirectBlock *indirect_block = (struct IndirectBlock *)(mappings[disk] + superblocks[disk]->d_blocks_ptr + file->blocks[IND_BLOCK]);
				printf("freeing indreict blocks\n");
				for (int i = 0; i < BLOCK_SIZE/sizeof(off_t); i++)
				{
					if(indirect_block->blocks[i] == -1){
						continue;
					}	
					block_num = indirect_block->blocks[i] / BLOCK_SIZE;
					printf("i: %d freeing indirect block %d\n", i, block_num);
					void *block = mappings[disk] + superblocks[disk]->d_blocks_ptr + indirect_block->blocks[i];
						
					if (memset(block, 0, BLOCK_SIZE) != block)
					{
						printf("unlink(): memset failed\n");
					}

					markbitmap_d(block_num, 0, disk);
				}
				block_num = file->blocks[IND_BLOCK] / BLOCK_SIZE;
				markbitmap_d(block_num, 0, disk);
				void *block = mappings[disk] + superblocks[disk]->d_blocks_ptr + indirect_block->blocks[IND_BLOCK];
				if (memset(block, 0, BLOCK_SIZE) != block)
				{
					printf("unlink(): memset failed\n");
				}
	
			}

			// Free the inode
			int inode_num = file->num;
			if (memset((void *)file, 0, BLOCK_SIZE) != (void *)file)
			{
				printf("unlink(): c0ing inode  failed\n");
			}
			markbitmap_i(inode_num, 0, disk);
		}

		// remove the directory entry to the file
		
	
//		if (deleteDentry(directory, file_name, disk) != 0)
//		{
//			printf("failed to remove file's dentry from dir\n");
//			return -1;
//		}
	}
	return 0;
}
static int wfs_mknod1(const char *path, mode_t mode, dev_t rdev)
{
	for (int disk = 0; disk < numdisks; disk++)
	{
		printf("wfs_mknod\n");
		char *malleable_path;
		Path *p;
		char *dir_name;
		struct wfs_inode *parent;
		struct wfs_inode *child;

		// Making path modifiable
		malleable_path = strdup(path);
		if (malleable_path == NULL)
		{
			printf("couldnt get malleable path\n");
			return -1;
		}

		p = splitPath(malleable_path); // Break apart path

		for (int i = 0; i < p->size; i++)
		{
			printf("Path component [%d]: %s\n", i, p->path_components[i]);
		}

		if (getInodePath(p, disk) != NULL)
		{
			printf("File already exists\n");
			return -EEXIST;
		}

		// Strip last element but save name
		dir_name = p->path_components[p->size - 1];
		p->size--;

		parent = getInodePath(p, disk);
		if (parent == NULL)
		{
			printf("Error getting parent\n");
			return -1;
		}

		child = allocateInode(disk);
		if (child == NULL)
		{
			printf("Error allocating child\n");
			return -ENOSPC;
		}

		child->mode |= mode;

		if (linkdir(parent, child, dir_name, disk) == -1)
		{
			printf("Linking error\n");
		}
	}

	printf("mknod done\n");
	return 0;
}

static int wfs_mknod0(const char *path, mode_t mode, dev_t rdev)
{

	printf("wfs_mknod\n");
	char *malleable_path;
	Path *p;
	char *dir_name;
	struct wfs_inode *parent;
	struct wfs_inode *child;

	// Making path modifiable
	malleable_path = strdup(path);
	if (malleable_path == NULL)
	{
		printf("couldnt get malleable path\n");
		return -1;
	}

	p = splitPath(malleable_path); // Break apart path

	for (int i = 0; i < p->size; i++)
	{
		printf("Path component [%d]: %s\n", i, p->path_components[i]);
	}

	if (getInodePath(p, 0) != NULL)
	{
		printf("File already exists\n");
		return -EEXIST;
	}

	// Strip last element but save name
	dir_name = p->path_components[p->size - 1];
	p->size--;

	parent = getInodePath(p, 0);
	if (parent == NULL)
	{
		printf("Error getting parent\n");
		return -1;
	}

	child = allocateInode(0);
	if (child == NULL)
	{
		printf("Error allocating child\n");
		return -ENOSPC;
	}

	printf("Child number is %d\n", child->num);

	child->mode |= mode;

	if (linkdir(parent, child, dir_name, 0) == -1)
	{
		printf("Linking error\n");
	}

	struct wfs_inode* first; 
	struct wfs_inode* second;
	for(int k = 1; k < numdisks;k++) {
	first = (struct wfs_inode*)(mappings[0] + superblocks[0]->i_blocks_ptr);
	second = (struct wfs_inode*)(mappings[k] + superblocks[k]->i_blocks_ptr);
		for(int i = 0; i < superblocks[k]->num_inodes;i++){
			memcpy(second, first, sizeof(struct wfs_inode));
			printf("First num %p\nSecond num %p\n", first, second);
			first = (struct wfs_inode*)((char*)first + BLOCK_SIZE);
			second = (struct wfs_inode*)((char*)second + BLOCK_SIZE);
		}

		// Copying inode bitmaps
		memcpy(superblocks[k], superblocks[0], superblocks[0]->d_bitmap_ptr);		
	}

	printf("mknod done\n");
	
	return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	if(raid_mode == 0) {
		return wfs_mknod0(path, mode, rdev);
	}
	else if(raid_mode == 1) {
		return wfs_mknod1(path, mode, rdev);
	}
	return 0;
}

static int wfs_rmdir(const char *path)
{

	for(int disk = 0;disk<numdisks;disk++) {
		
	
		printf("wfs_rmdir()\n");

		char* malleable_path;
		Path* p;
		char* dir_name;
		struct wfs_inode* my_inode;
		struct wfs_inode* parent;
		malleable_path = strdup(path);
		if(malleable_path == NULL) {
			printf("Cant get path for dir\n");
			return -1;
		}

		p = splitPath(malleable_path);
		if(p == NULL) {
			printf("Couldn't split path\n");
			return -1;
		}

		// Getting the dir to be removed
		my_inode = getInodePath(p, disk);
		if(my_inode == NULL) {
			printf("Error allocating inode in rmdir\n");
			return -1;
		}

		// Allocating child name
		dir_name = strdup(p->path_components[p->size-1]);
		if(dir_name == NULL) {
			printf("Dir name couldnt alloc in rmdir\n");
			return -1;
		}

		// Getting the parent
		p->size--;
		parent = getInodePath(p, disk);
		if(parent == NULL) {
			printf("Error allocating parent in rmdir\n");
			return -1;
		}

		// Check if it is a dir
		if( (my_inode->mode & S_IFDIR) == 0) {
			printf("Error cant rmdir on a non-dir\n");
			return -1;
		}

		// Check if its empty
		if(my_inode->size != 0) {
			printf("Error, dir not empty\n");
			return -1;
		}

		// Getting the directory entry in the parent
		struct wfs_dentry* my_dirent;
		my_dirent = searchDir(parent, dir_name,disk);
		if(my_dirent == NULL) {
			printf("Entry not found in rmdir\n");
			return -1;
		}

		int block;
		
		// Free the entry in the parent dir
		memset(my_dirent,0, sizeof(struct wfs_dentry));	
		parent->size-=sizeof(struct wfs_dentry);	
		for(int i =0; i < N_BLOCKS;i++) {
			if(my_inode->blocks[i] != -1) {
				block = my_inode->blocks[i] / BLOCK_SIZE;
				markbitmap_d(block, 0, disk);
			}
		}

		markbitmap_i(my_inode->num, 0, disk); // Freeing inode
		wfs_unlink(path); // Removing it in parent?
	}
	return 0;
}

static int readdir0(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	printf("=-----------WFS_READDIR0()---------\n");
	char *pathcpy = strdup(path);
	Path *p = splitPath(pathcpy);
	if (p == NULL)
	{
		printf("wfs_readdir(): path is null\n");
	}
	//TODO; FIx getInodePath
	struct wfs_inode *directory = getInodePath(p, 0);
	if (directory == NULL)
	{
		return -ENOENT;
	}

	if ((directory->mode && S_IFDIR) == 0)
	{
		return -EBADF;
	}

	off_t next_offset = 0;
	struct wfs_dentry *direntry;
	int original_offset = offset;
	int disk = 0;
	off_t filler_offset = 0;
	
	// return 0 if empty directory or if no more direntries
	while (1)
	{
		direntry = findNextDir0(directory, offset, &next_offset,disk );
		filler_offset += sizeof(struct wfs_dentry);
		
		//if the directry on this disk is empty
		if (direntry == NULL)
		{
			disk++;
			// if it is the end of the disk then exit
			if(disk >= numdisks){
				return 0;
			}
			continue;
		}


		printf("readdir0(): disk: %d direntry->name: %s num: %d\n, next_offset: %ld filler_offset: %ld\n", 
									disk, direntry->name, direntry->num, next_offset, filler_offset);

		
		if (filler(buf, direntry->name, NULL, 0) != 0)
		{
			printf("wfs_readdir(): filler returned nonzero\n");
			return 0;
		}

		// if at the end of the dir for this disk
		if (next_offset == 0)
		{
			disk++;
			if(disk == numdisks){
				return 0;
			}
		}

		offset = next_offset;
	}

	printf("wfs_readdir(): failed somehow\n");
	return 0;
}
// Return one or more directory entries (struct dirent) to the caller
// It is related to, but not identical to, the readdir(2) and getdents(2) system calls, and the readdir(3) library function. Because of its complexity, it is described separately below. Required for essentially any filesystem,
//  since it's what makes ls and a whole bunch of other things work.
// It's also important to note that readdir can return errors in a number of instances; in particular it can return -EBADF if the file handle is invalid, or -ENOENT if you use the path argument and the path doesn't exist.

// We shall let offset = the offset from the  d_blocks_ptr to the first dentry
// if offset == 0 then we search for the first dentry and set the offset to the next dentry
static int readdir1(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	printf("WFS_READDIR()---------\n");
	char *pathcpy = strdup(path);
	Path *p = splitPath(pathcpy);
	if (p == NULL)
	{
		printf("wfs_readdir(): path is null\n");
	}
	struct wfs_inode *directory = getInodePath(p, 0);
	if (directory == NULL)
	{
		return -ENOENT;
	}

	if ((directory->mode && S_IFDIR) == 0)
	{
		return -EBADF;
	}

	off_t next_offset = 0;
	struct wfs_dentry *direntry;
	int original_offset = offset;
	while (1)
	{
		
		direntry = findNextDir1(directory, offset, &next_offset);

		if (direntry == NULL)
		{
			printf("empty dir\n");
			return 0;
		}
		printf("readdir(): direntry->name: %s num: %d\n, next_offset: %ld\n", direntry->name, direntry->num, next_offset);
		if (filler(buf, direntry->name, NULL, next_offset) != 0)
		{
			printf("wfs_readdir(): filler returned nonzero\n");
			return 0;
		}

		if (next_offset == 0)
		{
			if(original_offset > 0){
				offset = 0;
				printf("original offset > 0\n");
				continue;
			}
			printf("wfs_readir(): no more files\n");
			return 0;
		}
		offset = next_offset;
	}
	printf("wfs_readdir(): failed somehow\n");
	return -1;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
	if(raid_mode == 1){
		return readdir1(path, buf, filler,  offset, fi);
	} else if (raid_mode == 0){
		return readdir0(path, buf, filler, offset, fi);
	}
	return -1;
}
static int read1(const char* path, char* buf, size_t size, off_t offset) {
	int bytes_read = 0;

	// Getting path components
	char* malleable_path = strdup(path);
	if(malleable_path == NULL) {
		printf("Couldn't get malleable path in read\n");
		return -1;
	}
	
	Path* p = splitPath(malleable_path);
	if(p == NULL) {
		printf("Couldn't get path struct in read\n");
		return -1;
	}

	struct wfs_inode* my_inode = getInodePath(p, 0);
	if(my_inode == NULL) {
		printf("Couldnt get inode of file to read\n");
		return -1;
	}

	// Check if offset too far out
	if(offset >= my_inode->size) {
		printf("Inode size is %ld\n", my_inode->size);
		return 0;
	}

	int data_index;
	unsigned char* data_ptr; // Points to the byte of data to be read

	//INDIRECT EXTENSION
	int indirect_block_index = -1;
	struct IndirectBlock * indirblock = NULL;

	data_index = offset / BLOCK_SIZE; // Going into the block which has this data
	if(data_index >= IND_BLOCK){
		indirect_block_index = data_index - D_BLOCK;
		indirblock =(struct IndirectBlock *)( mappings[0] + superblocks[0]->d_blocks_ptr + my_inode->blocks[IND_BLOCK]);
		data_ptr = mappings[0] + superblocks[0]->d_blocks_ptr + indirblock->blocks[indirect_block_index];
	} else {
		data_ptr = mappings[0] + superblocks[0]->d_blocks_ptr + my_inode->blocks[data_index] + (offset%BLOCK_SIZE);
	}

	int remaining = BLOCK_SIZE - (offset % BLOCK_SIZE);
	int offsetcpy = offset;
	while(bytes_read < size && offsetcpy < my_inode->size) {
		// Write up to end of block
		if(remaining > 0) {
			memcpy(buf + bytes_read, data_ptr, 1);
			bytes_read++;
			remaining--;
			data_ptr++;
			offsetcpy++;

		}
		else if(remaining == 0) {
			data_index++;
			if(data_index >= IND_BLOCK){
				indirect_block_index++;
				indirblock =(struct IndirectBlock *)( mappings[0] + superblocks[0]->d_blocks_ptr + my_inode->blocks[IND_BLOCK]);
				data_ptr = mappings[0] + superblocks[0]->d_blocks_ptr + indirblock->blocks[indirect_block_index];
			} else {
				data_ptr = mappings[0] + superblocks[0]->d_blocks_ptr + my_inode->blocks[data_index];
			}
			remaining = BLOCK_SIZE;
		}
		else {
			printf("Its cooked\n");
			return -1;
		}
	}

	if(*data_ptr == 0) {
		printf("Exit because of null\n");
	}
	else {
		printf("Exit because of eof\n");
	}
	return bytes_read;
}

static int read0(const char* path, char* buf, size_t size, off_t offset) {
	int bytes_read = 0;
	int disk = 0;
	// Getting path components
	char* malleable_path = strdup(path);
	if(malleable_path == NULL) {
		printf("Couldn't get malleable path in read\n");
		return -1;
	}
	
	Path* p = splitPath(malleable_path);
	if(p == NULL) {
		printf("Couldn't get path struct in read\n");
		return -1;
	}

	struct wfs_inode* my_inode = getInodePath(p, disk);
	if(my_inode == NULL) {
		printf("Couldnt get inode of file to read\n");
		return -1;
	}

	// Check if offset too far out
	if(offset >= my_inode->size) {
		printf("Inode size is %ld\n", my_inode->size);
		return 0;
	}

	int data_index;
	unsigned char* data_ptr; // Points to the byte of data to be read

	//INDIRECT EXTENSION
	int indirect_block_index = -1;
	struct IndirectBlock * indirblock = NULL;

	data_index = offset / BLOCK_SIZE; // Going into the block which has this data

	// !TODO HANDLE INDIRECT
	if(data_index >= IND_BLOCK){
		indirect_block_index = data_index - D_BLOCK;
		indirblock =(struct IndirectBlock *)( mappings[disk] + superblocks[disk]->d_blocks_ptr + my_inode->blocks[IND_BLOCK]);
		data_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + indirblock->blocks[indirect_block_index];
	} else {

		// Getting entry at disk and offset
		disk = getEntryDisk(my_inode->blocks[data_index]);
		data_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(my_inode->blocks[data_index]) + (offset%BLOCK_SIZE);
	}

	int remaining = BLOCK_SIZE - (offset % BLOCK_SIZE);
	int offsetcpy = offset;
	while(bytes_read < size && offsetcpy < my_inode->size) {
		// Write up to end of block
		if(remaining > 0) {
			memcpy(buf + bytes_read, data_ptr, 1);
			bytes_read++;
			remaining--;
			data_ptr++;
			offsetcpy++;

		}
		else if(remaining == 0) {
			data_index++;

			// !TODO HANDLE INDIRECT
			if(data_index >= IND_BLOCK){
				indirect_block_index++;
				indirblock =(struct IndirectBlock *)( mappings[disk] + superblocks[disk]->d_blocks_ptr + my_inode->blocks[IND_BLOCK]);
				data_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + indirblock->blocks[indirect_block_index];
			} else {
				disk = getEntryDisk(my_inode->blocks[data_index]);
				data_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(my_inode->blocks[data_index]);
			}
			remaining = BLOCK_SIZE;
		}
		else {
			printf("Its cooked\n");
			return -1;
		}
	}

	if(*data_ptr == 0) {
		printf("Exit because of null\n");
	}
	else {
		printf("Exit because of eof\n");
	}
	return bytes_read;	
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("wfs_read\n");
	if(raid_mode == 1 ) {
		return read1(path, buf, size, offset);
	}
	else if(raid_mode == 0) {
		return read0(path, buf, size, offset);
	}
	return -1;
}
static int write_raid0(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	printf("Write raid0\n");
	printf("Size is %d\n", size);
	printf("offset is %d\n");
	int written_bytes = 0;
	int disk =0;
	Path* p;
	char* malleable_path;
	struct wfs_inode* my_file;
	off_t curr_block_offset;
	unsigned char *curr_block_ptr;
	int remaining_space;
	int curr_block_index;

	malleable_path = strdup(path);
	if (malleable_path == NULL)
	{
		printf("Couldnt create malleable_path\n");
		return -1;
	}

	p = splitPath(malleable_path);
	if (p == NULL)
	{
		printf("Couldnt split path\n");
		return -1;
	}

	my_file = getInodePath(p, disk);
	printf("my_file->num: %d\n", my_file->num);
	if (my_file == NULL)
	{
		printf("File does not exist\n");
		return -ENOENT;
	}

	curr_block_index = offset / 512;
	curr_block_offset = my_file->blocks[curr_block_index];
	if(curr_block_offset == -1) {
		printf("No block found for write, we are going to grow the file\n");
		disk = getNextDisk();
		printf("Allocated disk is %d\n", disk);
		my_file->blocks[curr_block_index] = allocateBlock(disk);
		if(my_file->blocks[curr_block_index] == -1) {
			printf("No space to write\n");
			return -ENOSPC;
		}
		curr_block_offset = my_file->blocks[curr_block_index];
	}
	
	curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(curr_block_offset); 
	disk = getEntryDisk(curr_block_offset);
	remaining_space = BLOCK_SIZE -  (offset % BLOCK_SIZE);
	while(written_bytes != size) {
		printf("Loop\n");
		if(curr_block_offset == -1){
			disk = getNextDisk();
			printf("in while\n");
			printf("Allocated disk is %d\n", disk);
			my_file->blocks[curr_block_index] = allocateBlock(disk);
			curr_block_offset = my_file->blocks[curr_block_index];
			if (curr_block_offset == -1) { // If still not allocated then exit on error of no space
				return -ENOSPC;
			}
			curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(curr_block_offset);
		}


		if (size - written_bytes >= remaining_space) {
			printf("writing entire block, going to next blockn\n");
			memcpy(curr_block_ptr, buf + written_bytes, remaining_space); // Fill rest of block
			written_bytes += remaining_space;							  // Update how many bytes we have written
			curr_block_index++;
			curr_block_offset = my_file->blocks[curr_block_index];
			if(curr_block_offset != -1) {
				curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + getEntryOffset(curr_block_offset);
			}		
			remaining_space = BLOCK_SIZE;
		}

		// Write is less than remaining space in block
		else if ((size - written_bytes) < remaining_space)
		{
			printf("writing partial block\n");
			memcpy(curr_block_ptr, buf + written_bytes, size - written_bytes); // just write the bytes
			written_bytes += (size - written_bytes);
		}
		else
		{
			printf("Error you cant write more ... you shouldn't be here\n");
			return -1;
		}	
		
	}
	my_file->size += written_bytes;	
	printf("Wrote %d bytes\n", written_bytes);
	return written_bytes;
}


static int write_raid1(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	
	int written_bytes = 0;
	for(int disk =0; disk < numdisks;disk++) {
		
		written_bytes = 0;
		Path* p;
		char* malleable_path;
		struct wfs_inode* my_file;
		off_t curr_block_offset;
		unsigned char *curr_block_ptr;
		int remaining_space;
		int curr_block_index;

		malleable_path = strdup(path);
		if (malleable_path == NULL)
		{
			printf("Couldnt create malleable_path\n");
			return -1;
		}

		p = splitPath(malleable_path);
		if (p == NULL)
		{
			printf("Couldnt split path\n");
			return -1;
		}

		my_file = getInodePath(p, disk);
		printf("my_file->num: %d\n", my_file->num);
		if (my_file == NULL)
		{
			printf("File does not exist\n");
			return -ENOENT;
		}

		// indirect block stuff
		int indirect_block_index = -1;
		struct IndirectBlock * idirblock =(struct IndirectBlock *)( mappings[disk] +
			 superblocks[disk]->d_blocks_ptr + my_file->blocks[IND_BLOCK]); 

		curr_block_index = offset / BLOCK_SIZE;

		if(curr_block_index >= IND_BLOCK){
			indirect_block_index = curr_block_index - IND_BLOCK;
			curr_block_offset = idirblock->blocks[indirect_block_index]; 

		} else {
			curr_block_offset = my_file->blocks[curr_block_index];
		}

		if (curr_block_offset == -1)
		{															 // If block not allocated
			//CHECK IF AN INDIRECT BLOCK
			if(curr_block_index == IND_BLOCK){
				//allocate indirect block 
				my_file->blocks[curr_block_index] = initializeIndirectBlock( disk);	

				idirblock->blocks[0] = allocateBlock(disk);
				curr_block_offset = idirblock->blocks[0];
				indirect_block_index = 0;

			} else if (indirect_block_index >= 0){
				//allocate block in indirect block
				idirblock->blocks[indirect_block_index] = allocateBlock(disk);
				curr_block_offset = idirblock->blocks[indirect_block_index];
			} else  {
				my_file->blocks[curr_block_index] = allocateBlock(disk); // try to allocate it
				curr_block_offset = my_file->blocks[curr_block_index];
			}
				if (curr_block_offset== -1)
				{ // If still not allocated then exit on error of no space
					printf("Cant allocate more file for write\n");
					return -ENOSPC;
				}			
		}

		curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset + (offset%512);
		remaining_space = 512-(offset % 512);
		while(written_bytes != size) {

			// INDIRECT MOD

			// Ensuring block is allocated
			if (curr_block_offset == -1)
			{
				if(curr_block_index == IND_BLOCK){
					my_file->blocks[curr_block_index] = initializeIndirectBlock( disk);	
					idirblock = (struct IndirectBlock *)(mappings[disk]
					 + superblocks[disk]->d_blocks_ptr + my_file->blocks[curr_block_index]);

					idirblock->blocks[0] = allocateBlock(disk);
					curr_block_offset = idirblock->blocks[0];
					indirect_block_index = 0;

				} else if (indirect_block_index >= 0){
					idirblock->blocks[indirect_block_index] = allocateBlock(disk);	
					curr_block_offset = idirblock->blocks[indirect_block_index];
				} else {
					my_file->blocks[curr_block_index] = allocateBlock(disk);
					curr_block_offset = my_file->blocks[curr_block_index];
				}

				if (curr_block_offset == -1)
				{ // If still not allocated then exit on error of no space
					return -ENOSPC;
				}
				curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset;
			}


			// Check if we need to write larger than block space
			if (size - written_bytes >= remaining_space)
			{
				memcpy(curr_block_ptr, buf + written_bytes, remaining_space); // Fill rest of block
				written_bytes += remaining_space;							  // Update how many bytes we have written

				// Go to next block
				if(indirect_block_index >= 0){
					indirect_block_index++;
					curr_block_offset = idirblock->blocks[indirect_block_index];
					curr_block_index++;
				} else {
					curr_block_index++;
					curr_block_offset = my_file->blocks[curr_block_index];
				}
				curr_block_ptr = mappings[disk] + superblocks[disk]->d_blocks_ptr + curr_block_offset;
				remaining_space = BLOCK_SIZE;
			}


			// Write is less than remaining space in block
			else if (size - written_bytes < remaining_space)
			{
				memcpy(curr_block_ptr, buf + written_bytes, size - written_bytes); // just write the bytes
				written_bytes += (size - written_bytes);
			}
			else
			{
				printf("Error you cant write more ... you shouldn't be here\n");
				return -1;
			}	
		}
		my_file->size += written_bytes;	
	}
	
	return written_bytes;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){

	if(raid_mode == 1){
		printf("raid1\n");
		return write_raid1(path, buf, size, offset, fi);
	}
	else if(raid_mode == 0) {
		return write_raid0(path, buf, size, offset, fi);
	}
	return -1;

}
static int wfs_getattr(const char *path, struct stat *stbuf)
{
	printf("wfs_getattr\n");
	printf("Path is %s\n", path);
	Path *p;
	struct wfs_inode *my_inode;
	char *malleable_path;
	malleable_path = strdup(path);

	if (malleable_path == NULL)
	{
		return -1;
	}
	p = splitPath(malleable_path);
	if(p== NULL) {
		return -1;
	}
	
	my_inode = getInodePath(p, 0);
	if (my_inode == NULL)
	{
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
	return 0;
}

static struct fuse_operations ops = {
	.getattr = wfs_getattr,
	.mknod = wfs_mknod,
	.mkdir = wfs_mkdir,
	.unlink = wfs_unlink,
	.rmdir = wfs_rmdir,
	.read = wfs_read,
	.write = wfs_write,
	.readdir = wfs_readdir,
	.destroy = wfs_destroy,
};


int main(int argc, char *argv[])
{
	// FOR VALGRIND
	// argc = argc-1;
	// argv = &argv[1];

	int new_argc; // Used to pass into fuse_main

	// TODO: INITIALIZE Raid_mode
	mapDisks(argc, argv);

	new_argc = (argc - numdisks); // Gets difference of what was already read vs what isnt
	char *new_argv[new_argc];
	new_argv[0] = "./wfs";
	for (int j = 1; j < new_argc; j++)
	{
		new_argv[j] = argv[numdisks + j];
	}

	for (int i = 0; i < new_argc; i++)
	{
		printf("New arg [%d]: %s\n", i, new_argv[i]);
	}

	printf("Num disks %d\n", numdisks);
	for (int i = 0; i < numdisks; i++)
	{
		printf("Disk [%d]: %d\n", i, disk_size[i]);
	}

	return fuse_main(new_argc, new_argv, &ops, NULL);

}
