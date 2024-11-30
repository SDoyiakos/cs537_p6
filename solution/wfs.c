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
static struct wfs_inode * curr_inode;

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
// Look for a directory entry in a directory.
 // If found, set *poff to byte offset of entry.
// struct inode*
// dirlookup(struct inode *dp, char *name, uint *poff)
// {
//   uint off, inum;
//   struct dirent de;
// 
//   if(dp->type != T_DIR)
//     panic("dirlookup not DIR");
// 
//   for(off = 0; off < dp->size; off += sizeof(de)){
//     if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
//       panic("dirlookup read");
//     if(de.inum == 0)
//       continue;
//     if(namecmp(name, de.name) == 0){
//       // entry matches path element
//       if(poff)
//         *poff = off;
//       inum = de.inum;
//       return iget(dp->dev, inum);
//     }
//   }
// 
//   return 0;
// }
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

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
//static struct inode*
//namex(char *path, int nameiparent, char *name)
//{
//  struct inode *ip, *next;
//
//  if(*path == '/')
//    ip = iget(ROOTDEV, ROOTINO);
//  else
//    ip = idup(myproc()->cwd);
//
//  while((path = skipelem(path, name)) != 0){
//    ilock(ip);
//    if(ip->type != T_DIR){
//      iunlockput(ip);
//      return 0;
//    }
//    if(nameiparent && *path == '\0'){
//      // Stop one level early.
//      iunlock(ip);
//      return ip;
//    }
//    if((next = dirlookup(ip, name, 0)) == 0){
//      iunlockput(ip);
//      return 0;
//    }
//    iunlockput(ip);
//    ip = next;
//  }
//  if(nameiparent){
//    iput(ip);
//    return 0;
//  }
//  return ip;
//}

static struct wfs_inode * iget(int inum){
	// TODO: check if inode is in bitmap

	// get the inode
	off_t inode_offset = superblocks[0]->i_blocks_ptr + (512 * inum);
	struct wfs_inode * i = malloc(sizeof(struct wfs_inode));
	if(i == NULL){
		printf("failed to malloc i\n");
		exit(1);
	}	

	if(pread(disks[0], i, sizeof(struct wfs_inode), inode_offset) == -1){
		printf("failed to get inode\n");
		exit(1);
	}	

	return i;
}

// If wantparent = 0, this method returns the inode at the end of the path. If wantparent != 0 it 
// returns the parent of the inode at the end of the path. 
// eg: wfs_namex(/a/b/c, 0, name) = inod w name c. Name = c. 
//     wfs)namex(/a/b/c, 1, name) = inode at b. name - c
static struct wfs_inode* wfs_namex(char* path, int wantparent, char* name){
	struct wfs_inode *curr_i, *next_i;
	
	if(*path == '/'){
		curr_i = iget(0);	
	}
	else {
		curr_i = current_inode;
	}

	while((path = skipelem(path, name)) != 0){
		
		if(wantparent && (*path == '\0'){
			return curr_i;
		}
		
		if((next_i = dirlookup(curr_i, name, 0)) == 0) return 0;
		
		curr_i = next_i

	}

}
//struct inode*
//namei(char *path)
//{
//  char name[DIRSIZ];
//  return namex(path, 0, name);
//}
//
//struct inode*
//nameiparent(char *path, char *name)
//{
//  return namex(path, 1, name);
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
	// get the inode of the parent
	// nameiparent()
	// check if the parent contains the name of the directory already
	// create and allocate the inode	
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
