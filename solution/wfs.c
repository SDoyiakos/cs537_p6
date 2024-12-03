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


static int ** disks;
static unsigned char** mappings;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;
static struct wfs_inode *current_inode;
static int DIRSIZ;




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

	int byte_dist = inum/8; // how many byes away from start inum is
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

static struct wfs_inode * dirlookup(struct wfs_inode *dp, char *name, uint *entry_offset) {

	// FOR RAID 1
	unsigned char* superblock_offset = mappings[0];
	struct wfs_sb * superblock = superblocks[0];	

	if((dp->mode && S_IFDIR) == 0){
		printf("not a directory\n");
		exit(1);	
	}

	for(int i = 0; i < N_BLOCKS; i++){
		if(dp->blocks[i] == 0) continue;

		//iterte through all dir entries in a blcok
		struct wfs_dentry * dir_entry;
		for(uint j = dp->blocks[i]; j < dp->blocks[i] + BLOCK_SIZE; j+= sizeof(struct wfs_dentry)){
			dir_entry = (struct wfs_dentry *)((superblock_offset)
						 +((unsigned char) j));		

			if(dir_entry->name ==NULL) {
				continue;
			}	

			if(strcmp(dir_entry->name, name) == 0){
				if(entry_offset){
					entry_offset = (void*)dir_entry;	
				}	
	
				return(iget((uint)dir_entry->num));		
			}	
		}
	}
	return 0;
}


// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct wfs_inode *dp, char *name, uint inum)
{
	printf("dirlink()\n");
	struct wfs_dentry * de;
	struct wfs_inode *ip;
	
	// Check that name is not present.
	if((ip = dirlookup(dp, name, 0)) != 0){
		printf("a file with the same name adly exists");
	  return -1;
	}
	
	printf("passes dirlookup\n");	
	// Look for an empty dirent.
	for(int i = 0; i < N_BLOCKS; i++){
		
		//alocate the first empty block
		printf("dp->blocks[i] = %d\n", dp->blocks[i]);
		if(dp->blocks[i] == 0){
			printf("allocatign new block\n");
			int new_data_num = findFreeData();
			printf("new_data_num: %d\n", new_data_num);
			dp->blocks[i] =  BLOCK_SIZE * new_data_num;		
			markbitmap_d(new_data_num, 1);
		}
	
		printf("block isnt empty\n");
		de = (struct wfs_dentry *)((unsigned char *) mappings[0] + dp->blocks[i]);
		printf("de: 0x%x\n",(uint)de);
		printf("de->name: %s\n", (uint)de->name);

		for (off_t block_offset = dp->blocks[i]; block_offset < ip->blocks[i] + BLOCK_SIZE; block_offset += sizeof(struct wfs_dentry)){
			de = (struct wfs_dentry *)( mappings[0] + block_offset);
			
			if(de->name == '\0'){
				printf("de->name == NULL\n");
				strcpy(de->name, name);
				de->num = inum;
				return 0;
			}	

		} 

	} 
	
	printf("file is full\n");
	return -1;
}
 // Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{ 
  char *s;
  int len;
  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}


static struct wfs_inode* namex(char *path, int nameiparent, char *name){
	struct wfs_inode *ip, *next;

	if(*path == '/'){
		ip = iget(0);
	}else {
		ip = current_inode;	
	}

	while((path = skipelem(path, name)) != 0){
		if((ip->mode && S_IFDIR) == 0){
			printf("namex failed. file is not a dir\n");
			exit(1);
		}

		if(nameiparent && (*path == '\0')){
			return ip;
		}
		
		if((next = dirlookup(ip, name, 0)) == 0){
			return 0;
		}
		ip = next;
	}

	return ip;
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

static int wfs_mkdir(const char* path, mode_t mode)
{

	printf("wfs_mkdir()\n");
	char * name = malloc(sizeof(char) * FILE_NAME_MAX);
	struct wfs_inode *parent = namex(path, 1, name);			

	printf("parent->num: %d\n", parent->num);
	// initialize new directory
	int dnum = findFreeInode();
	struct wfs_inode * idir = (mappings[0]) +
								(uintptr_t)superblocks[0]->i_blocks_ptr +
								(uintptr_t)(512 * dnum);
	
	idir->num = dnum;
	idir->mode = S_IFDIR;
	idir->uid = getuid(); 
	idir->gid = getgid(); 
	idir->size = sizeof(struct wfs_inode) + (512 * 2); 
	idir->nlinks = 1; 
	time_t t_result = time(NULL);
	idir->atim = t_result; 
	idir->mtim = t_result;
	idir->ctim = t_result;


	printf("idir->num: %d\n", idir->num);
	// LINK PARENT TO CHILD
	dirlink(parent, name, idir->num); 
	printf("first dirklink passed\n");	
	// LINK CHILD TO PARENT
	dirlink(idir, "..", parent->num); 
	printf("first dirklink passed\n");	
	// set one datablock to parent inode

	//update bitmaps
	markbitmap_i(dnum, 1);
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

int test_markbitmapi(){
	printf("test_markbitmap()\n");
	printf("expected: 00000001 00000000 00000000 00000000\n  actual: ");
	print_ibitmap();
	printf("\n");

	markbitmap_i(1, 1);
	markbitmap_i(31, 1);
	printf("expected: 00000011 00000000 00000000 10000000\n  actual: ");
	print_ibitmap();	
	printf("\n");

	markbitmap_i(1, 0);
	markbitmap_i(31, 0);
	printf("expected: 00000001 00000000 00000000 00000000\n  actual: ");
	print_ibitmap();	
	return 0;
}
 int test_mkdir(){

	printf("test_mkdir()\n");
	wfs_mkdir("/hello", S_IFDIR);	
	//inode bitmap should be updated
	printf("inode bitmap should be updated\n");
	printf("expected: 00000011 00000000 00000000 00000000\n  actual: ");
	print_ibitmap();
	printf("\n");
	//data bitmaps should be updated
	printf("data bitmap should be updated\n");
	printf("expected: 00000111 00000000 00000000 00000000\n  actual: ");
	print_dbitmap();
	printf("\n");

	printf("root inode should have a dir entry for the new directory\n");
	printf("expected: name: Hello num: 1\n");
	struct wfs_dentry * p_de = (struct wfs_dentry *)(mappings[0] + roots[0]->blocks[1]);
	printf("  actual: name: %s num: %d\n", p_de->name, p_de->num);

	struct wfs_inode * dir_inode = iget(p_de->num);
	printf("dir entry inode should be initialized properly\n");
	printf("expected: num: 1 mode: %d size: %d\n", S_IFDIR, 512 * 3);
	printf("  actual: num: %d mode : %d size %d\n", dir_inode->num, dir_inode->mode, dir_inode->size); 
	//directory mode should be S_IFDIR

	printf("dir entry inode should have an entry to the parent inode\n");
	printf("expected: num: 0 name: ..\n");
	struct wfs_dentry * dir_dep = dir_inode->blocks[0];
	printf("  actual: num: %d name: %s\n", dir_dep->num, dir_dep->name);	

	//blocks should point to the dataentries
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
	while(i < argc && argv[i][0] != '-'){ 

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
	
	printf("after while\n");
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
	printf("end mapdisks\n");
	return i;

}


int main(int argc, char* argv[])
{
	int new_argc; // Used to pass into fuse_main
	
	new_argc = argc - mapDisks(argc, argv); // Gets difference of what was already read vs what isnt
	new_argc = argc - new_argc; 
	char* new_argv[new_argc];

	// initialize currentInode
	current_inode = roots[0];
	DIRSIZ = superblocks[0]->num_data_blocks / superblocks[0]->num_inodes;

	for(int j = 0;j < new_argc; j++) {
		new_argv[j] = argv[1 + numdisks + j];
	}
	
  printf("dirlookup() test. \n Expected: 0\n actual: ");
	uint offset = NULL;
	struct wfs_inode * firstentry =  dirlookup(roots[0], "hello", &offset);

	char * name = malloc(28 * sizeof(char));	
	printf("namex() test: \n Expected: inodeNum = 0, Name = hello \n actual: %d %s\n", namex("/hello", 1, name)->num, name); 
	

	test_markbitmapi();
	test_mkdir();
	return fuse_main(new_argc, new_argv, &ops, NULL);	
}
