#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "wfs.h"
#include <unistd.h> 
#include <time.h>
//This C program initializes a file to an empty filesystem. I.e. to the state, where the filesystem can be mounted and other files and directories can be created under the root inode. The program receives three arguments: the raid mode, disk image file (multiple times), the number of inodes in the filesystem, and the number of data blocks in the system. The number of blocks should always be rounded up to the nearest multiple of 32 to prevent the data structures on disk from being misaligned. For example:

//./mkfs -r 1 -d disk.img -d disk.img -i 32 -b 200
//initializes all disks (disk1 and disk2) to an empty filesystem with 32 inodes and 224 data blocks. The size of the inode and data bitmaps are determined by the number of blocks specified by mkfs. If mkfs finds that the disk image file is too small to accommodate the number of blocks, it should exit with return code -1. mkfs should write the superblock and root inode to the disk image./


int init_disks(int * disks, int num_disks, int num_inodes, int num_datablocks, int raid_mode){
	printf("sizeof(struct inode) = %ld sizeof(struct sb) = %ld \n", sizeof(struct wfs_inode), sizeof(struct wfs_sb));

	time_t t_result;
	for(int i = 0; i < num_disks; i++){
		// INIT THE SUPER BLOCK 
		struct wfs_sb * superblock = malloc(sizeof(struct wfs_sb)); 	
		superblock->num_inodes = num_inodes;
		superblock->num_data_blocks = num_datablocks;
		superblock->i_bitmap_ptr = sizeof(struct wfs_sb);

        int i_bitmap_size = num_inodes /8;
        int d_bitmap_size = num_datablocks /8;
		superblock->d_bitmap_ptr = superblock->i_bitmap_ptr + (i_bitmap_size);

		//inode offset is a multiple of 512
		off_t inode_offset = sizeof(struct wfs_sb) + (i_bitmap_size) + (d_bitmap_size);
		int remainder = inode_offset % 512;
		if(remainder != 0) inode_offset = inode_offset + (512 - remainder);
		superblock->i_blocks_ptr = inode_offset;

		off_t datablocks_offset = inode_offset + (512 * num_inodes);
		superblock->d_blocks_ptr = datablocks_offset;	

		superblock->raid_mode = raid_mode;
		
        //INIT THE ROOT DIR.
		struct wfs_inode * root_inode = malloc(sizeof(struct wfs_inode));
		root_inode->num = 1;
		root_inode->mode = S_IRWXU;
		root_inode->uid = getuid();
		root_inode->gid = getgid();
		root_inode->size = 0;
		root_inode->nlinks = 1;
		
		t_result = time(NULL);
		root_inode->atim = t_result; 
		root_inode->mtim = t_result;
		root_inode->ctim = t_result; 

		if(write(disks[i], superblock, sizeof(struct wfs_sb)) == -1){
			printf("failed to write superblock to disk[%d]: %d\n", i, disks[i]);
			exit(1);
		};
		
		if(lseek(disks[i], superblock->i_blocks_ptr, SEEK_SET) == -1){
			printf("failed to lseek()\n");
			exit(1);
		}	
        
        if(write(disks[i], root_inode, sizeof(struct wfs_inode)) == -1){ printf("failed to write root_inode to disk[%d]: %d\n", i, disks[i]);
			exit(1);
		};
		

		if(lseek(disks[i], superblock->i_bitmap_ptr, SEEK_SET) == -1){
			printf("failed to lseek()\n");
			exit(1);
		}	

        char i_bitmap[i_bitmap_size];
        for(int i = 0; i < i_bitmap_size; i++){
            i_bitmap[i]= 0x00;
        }
        i_bitmap[0] = 0x10;

        if(write(disks[i], &i_bitmap, sizeof(char) * i_bitmap_size)  == -1){ 
            printf("failed to write bitmap to disk[%d]: %d\n", i, disks[i]);
			exit(1);
		};


		printf("test that superblock is in\n");
		struct wfs_sb * read_sb = malloc(sizeof(struct wfs_sb));
		pread(disks[i], read_sb, sizeof(struct wfs_sb), 0);
		printf("wfs_sb->num_inodes = %ld num_datablocks: %ld it_bitmap_ptr: %ld d_bitmap_ptr: %ld i_blocks_ptr: %ld d_blocks_ptr: %ld\n", read_sb->num_inodes, read_sb->num_data_blocks,read_sb->i_bitmap_ptr,read_sb->d_bitmap_ptr,read_sb->i_blocks_ptr,read_sb->d_blocks_ptr);
        char *ri_bitmap = malloc(sizeof(char) * i_bitmap_size);
        pread(disks[i], ri_bitmap, i_bitmap_size * sizeof(char), read_sb->i_bitmap_ptr);
        printf("bitmap: %x\n", ri_bitmap[0]);
		close(disks[i]);				
	}

	
	return 0;	

}

// int bitmap(void){
//     char bitmap = 0x01;
//     char mask = 0x01;
//     bitmap ^= 1 << 3;

//     if((bitmap & mask) == 1) printf("mask works\n");

//     printf("bitmap: 0x%x\n", bitmap);
    
//     return 0;

// }
int main(int argc, char *argv[])
{
 
	if (argc < 3){
		exit(1);
	}
	
	int raid_mode = -1;
	int num_disks = 0;
	int * disks = malloc(sizeof(int));
	int num_inodes = -1;
	int num_datablocks = -1;
	for(int i = 0; i < argc; i++){

		if(argv[i][0] == '-'){

			if(argv[i][1] == 'r'){
				if(raid_mode != -1){
					printf("multiple arguments for raid\n");
					exit(1);
				}		
				raid_mode = atoi(argv[i + 1]);
				i++;
				continue;
			}
			
			if(argv[i][1] == 'd'){
				num_disks++;
				int fd = open(argv[++i], O_RDWR);
				if(fd == -1){
					printf("failed to open file %s\n", argv[i]);
					exit(1);
				}	
				if(reallocarray(disks, sizeof(int), num_disks) == NULL){
					printf("failed to reallocarray\n");
					exit(1);
				}
				disks[num_disks-1] = fd;
				continue;
			}
		
			if(argv[i][1] == 'i'){
				
				if(num_inodes != -1){
					printf("multiple arguments for num_inodes\n");
					exit(1);
				}		
				num_inodes = atoi(argv[i + 1]);
				i++;
				continue;

			}
	
			if(argv[i][1] == 'b'){
				
				if(num_datablocks != -1){
					printf("multiple arguments for num_datablocks\n");
					exit(1);
				}		
				num_datablocks = atoi(argv[i + 1]);
				int remainder = num_datablocks % 32;
				if(remainder !=0) num_datablocks = (32 -remainder) + num_datablocks;
				i++;
				continue;

			}
		}
	}	


	// printf("num_datablocsk: %d\n", num_datablocks);
	// printf("num_inodes: %d\n", num_inodes);
	// printf("raid_mode: %i\n", raid_mode);
	// for(int i = 0; i < num_disks; i++){
	// 	printf("disk fd: %d\n", disks[i]);
	// }
	 printf("init disks: %i\n", init_disks(disks, num_disks, num_inodes, num_datablocks, raid_mode));
	return 0; 
}
