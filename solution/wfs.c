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


int * disks;
int numdisks = 0;


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

	
	for(int i = 1; i < argc; i++){

		//read in the disks
		if(argv[i][0] != '-'){
			numdisks++;
			disks = realloc(disks, sizeof(int) * numdisks);
			int fd = open(argv[i], O_RDWR);
			if(fd != -1){
				disks[i] = fd;
			}
		}

	}

	for(int i = 0; i < numdisks; i++){
		printf("files: %d\n", disks[i]);
	}


	return fuse_main(argc, argv, &ops, NULL);	

}
