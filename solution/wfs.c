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
static unsigned char** mappings;
static int numdisks = 0;
static struct wfs_sb ** superblocks;
static struct wfs_inode **roots;
//static int follow_path(const char* path){
//
//	return 0;
//
//}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
//static struct inode*
//iget(uint dev, uint inum)
//{ 
//  struct inode *ip, *empty;
//  
//  acquire(&icache.lock);
//  
//  // Is the inode already cached?
//  empty = 0;
//  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
//    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
//      ip->ref++;
//      release(&icache.lock);
//      return ip;
//    }
//    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
//      empty = ip;
//  }
//
//  // Recycle an inode cache entry.
//  if(empty == 0)
//    panic("iget: no inodes");
//
//  ip = empty;
//  ip->dev = dev;
//  ip->inum = inum;
//  ip->ref = 1;
//  ip->valid = 0;
//  release(&icache.lock);
//
//  return ip;
//}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
//struct inode*
//dirlookup(struct inode *dp, char *name, uint *poff)
//{ 
//  uint off, inum;
//  struct dirent de;
//  
//  if(dp->type != T_DIR)
//    panic("dirlookup not DIR");
//  
//  for(off = 0; off < dp->size; off += sizeof(de)){
//    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
//      panic("dirlookup read");
//    if(de.inum == 0)
//      continue;
//    if(namecmp(name, de.name) == 0){
//      // entry matches path element
//      if(poff)
//        *poff = off;
//      inum = de.inum;
//      return iget(dp->dev, inum);
//    }
//  }
//  
//  return 0;
//}
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

static struct wfs_inode * dirlookup(struct wfs_inode *dp, char *name, uint *entry_offset) {

	// FOR RAID 1
	unsigned char* superblock_offset = mappings[0];
	struct wfs_sb * superblock = superblocks[0];	

	if((dp->mode && S_IFDIR) == 0){
		printf("not a directory\n");
		exit(1);	
	}

	for(int i = 0; i < N_BLOCKS; i++){
		struct wfs_dentry * dir_entry = (struct wfs_dentry *)((superblock_offset)
								 +((unsigned char)superblock->d_blocks_ptr)
								 +((unsigned char) dp->blocks[i]));		

		
		if(dir_entry->num == 0){
			 continue;
		}

		if(strcmp(dir_entry->name, name) == 0){
			if(entry_offset){
				entry_offset = (uint) dir_entry;	
			}	

			return(iget((uint)dir_entry->num));		
		}	
	}
	return 0;
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
//static char*
//skipelem(char *path, char *name)
//{ 
//  char *s;
//  int len;
//  
//  while(*path == '/')
//    path++;
//  if(*path == 0)
//    return 0;
//  s = path;
//  while(*path != '/' && *path != 0)
//    path++;
//  len = path - s;
//  if(len >= DIRSIZ)
//    memmove(name, s, DIRSIZ);
//  else {
//    memmove(name, s, len);
//    name[len] = 0;
//  }
//  while(*path == '/')
//    path++;
//  return path;
//}

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
		printf("roots[%d] inode mode_t %d\n", i, roots[i]->mode);
		
	}	
	
	argv = &argv[i];

	// Print values at root
	printf("Bit at index 1 is %d\n", checkIBitmap(0));
	printf("Root nlinks is %d\n", iget(0)->nlinks);

	printf("dirlookup() test. \n Expected: 0\n actual: ");
	uint offset = NULL;
	struct wfs_inode * firstentry =  dirlookup(roots[0], "hello", &offset);
	
	return fuse_main(argc, argv, &ops, NULL);	

}
