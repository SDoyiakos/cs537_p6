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
#include "wfs.h"

static int raid_mode = -1;
static int * disks;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;

static char** parse_path(const char* path, int* total_tokens){
	char * pathcpy = strdup(path);
	if(pathcpy == NULL) {
		printf("failed strdup\n");
		exit(1);
	}	
	int num_tokens = 0;
	int i = 0;
	while(pathcpy[i] != '\0'){
	
		if(pathcpy[i] == '/'){
			num_tokens++;
		}
		i++;	
	}	
	
	if(pathcpy[i-1] == '/') {
		printf("invalid path\n");
		exit(1);
	}

	memcpy(total_tokens, &num_tokens, sizeof(int));

	char ** path_split = malloc(sizeof(char*) * num_tokens);
	if(path_split == NULL) {
		printf("failed malloc\n");
		exit(1);
	}	
	char *tok = strtok(pathcpy, "/");
	if(tok == NULL) {
		printf("failed strtok\n");
		exit(1);
	}	

	i = 0;
	while(tok){
		path_split[i] = strdup(tok);
		i++;
		tok = strtok(NULL, "/");
	}
	free(pathcpy);
	return path_split;
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
	while(argv[i][0] != '-'){

		//read in the disks
		printf("argv[%d]: %s\n", i, argv[i]);
		numdisks++;
		disks = realloc(disks, sizeof(int) * numdisks);
		int fd = open(argv[i], O_RDWR);
		if(fd == -1){
			printf("failed to open file\n");
			free(argv[i]);
			exit(1);
		}
		disks[i-1] = fd;
		i++;
	}

	//INIT SUPERBLOCKS AND ROOT 
	roots = malloc(sizeof(struct wfs_inode*) * numdisks);
	superblocks = malloc(sizeof(struct wfs_sb*) * numdisks);
	for(int i = 0; i < numdisks; i++){
		superblocks[i] = malloc(sizeof(struct wfs_sb));
		roots[i] = malloc(sizeof(struct wfs_inode));

		if(pread(disks[i], superblocks[i], sizeof(struct wfs_sb), 0) == -1){
			printf("failed to read in superblock\n");
			exit(1);
		}

		if(pread(disks[i], roots[i], sizeof(struct wfs_inode), superblocks[i]->i_blocks_ptr) == -1){
			printf("failed to read in superblock\n");
			exit(1);
		}
	}	

	for(int i = 0; i < numdisks; i++){
		printf("superblock[%d] num_inodes: %ld\n", i, superblocks[i]->num_inodes);
		printf("roots[%d] inode num: %d\n", i, roots[i]->num);
	}	
	

	argv = &argv[i];
	return fuse_main(argc, argv, &ops, NULL);	

}
