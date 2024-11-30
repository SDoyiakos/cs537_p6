#define FUSE_USE_VERSION 30
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

static int * disks;
static void** mappings;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;
//static int follow_path(const char* path){
//
//	return 0;
//
//}
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


int main(int argc, char *argv[])
{

	int i = 1;
	while(i < argc && argv[i][0] != '-'){ // Read non-flags

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
	mappings = malloc(sizeof(void*)* numdisks); // Allocate region for beginning ptr in mappings
	if(mappings == NULL) {
		printf("Failed to allocate mapping addrs\n");
		exit(1);
	}
	superblocks = malloc(sizeof(struct wfs_sb*) * numdisks);
	if(superblocks == NULL) {
		printf("Failed to allocate superblocks\n");
		exit(1);
	}
	roots = malloc(sizeof(struct wfs_inode*) * numdisks);
	if(roots == NULL) {
		printf("Unable to allocate roots\n");
		exit(1);
	}

	// Map every disk into memory
	struct stat my_stat;
	for(int i = 0; i < numdisks;i++) {
		fstat(disks[i], &my_stat);
		mappings[i] = mmap(NULL, my_stat.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE,disks[i], 0);
		if(mappings[i] == MAP_FAILED) {
			printf("Error, couldn't mmap disk into memory\n");
			exit(1);
		}
		superblocks[i] = (struct wfs_sb*)mappings[i];
		roots[i] = (struct wfs_inode*)((char*)superblocks[i] + superblocks[i]->i_blocks_ptr);
	}

	// Print metadata
	for(int i = 0; i < numdisks; i++){
		printf("superblock[%d] num_inodes: %ld\n", i, superblocks[i]->num_inodes);
		printf("roots[%d] inode num: %d\n", i, roots[i]->num);
	}	
	
	argv = &argv[i];
	return fuse_main(argc, argv, &ops, NULL);	

}
