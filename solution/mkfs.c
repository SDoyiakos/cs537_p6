#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "wfs.h"

int raid_mode; // The type of RAID being used
int inode_count; // The number of inodes
int block_count; // The number of data blocks

// Bitmaps
char* inode_bitmap;
char* data_bitmap;


// Structs and vars for disk linked list
struct Disk {
	int dfile;
	struct Disk* next;
};
struct Disk* disk_head;
int disk_ct;



int parseRaid(char* str) {
	int raid_val;
	
	// The length of the int will be 1 char
	if(strlen(str) != 1) {
		return -1;
	}

	raid_val = *str - '0'; // Convert char to int
	if(raid_val != 0 && raid_val != 1) { // We are only doing 0 and 1	
		return -1;
	}

	return raid_val;
	
}

int parseArgs(int argc, char* argv[]) {
	char* curr_arg;
	
	// Iterate over all args
	for(int i = 1;i < argc - 1;i+=2) {
		curr_arg = argv[i];
		if(strcmp(curr_arg, "-r") == 0) {
			raid_mode = parseRaid(argv[i+1]);
		}
		else if(strcmp(curr_arg, "-d") == 0) {

			// Allocate new disk and error check
			struct Disk* new_disk;
			new_disk = malloc(sizeof(struct Disk));
			if(new_disk == NULL) {
				printf("Error, disk could not be allocated\n");
				exit(-1);
			}

			// Insert new disk data
			if(disk_head == NULL) {
				disk_head = new_disk;
				disk_head->next = NULL;
			}
			else {
				new_disk->next = disk_head;
				disk_head = new_disk;
			}

			disk_ct++;

			// Open file for read/write
			disk_head->dfile = open(argv[i+1], O_RDWR);
			if(disk_head->dfile == -1) {
				printf("Error opening file\n");
				exit(-1);
			}	
		}
		else if(strcmp(curr_arg, "-i") == 0) {
			inode_count = atoi(argv[i+1]);
			if(inode_count % 32 != 0) {
				inode_count += 32 - (inode_count % 32); // Fix to multiple of 32
			}
		}
		else if(strcmp(curr_arg, "-b") == 0) {
			block_count = atoi(argv[i+1]);
			if(block_count % 32 != 0) {
				block_count += 32 - (block_count % 32); // Fix to multiple of 32
			}
		}
		else {
			printf("Error, flags are not formatted properly\n");
			exit(1);
		}
	}

	// Checking all flags are set
	if( (raid_mode == -1) | (disk_ct == 0) | (inode_count == -1) | (block_count == -1) ) {
		printf("RAID: %d\nDisk ct: %d\nInode ct: %d\nBlock ct: %d\n",raid_mode, disk_ct, inode_count, block_count);
		printf("Error, not enough flags set\n");
		exit(1);
	}
	
	return 0;
}

void writeToDisk(struct wfs_sb* my_sb){
	struct Disk* curr_disk = disk_head;
	int curr_fd;
	struct wfs_inode curr_inode;
	time_t curr_time;

	// Allocating INode bitmap
	inode_bitmap = calloc(inode_count/8, 1); 
	if(inode_bitmap == NULL) {
		printf("Error, couldn't allocate inode bitmap\n");
		exit(-1);
	}
	*(inode_bitmap  + (inode_count/8) - 1) = 1; // Setting root

	// Allocating Data bitmap
	data_bitmap = calloc(block_count/8, 1);
	if(data_bitmap == NULL) {
		printf("Error, couldn't allocate data bitmap\n");
		exit(-1);
	}
	
	// Do write for each disk
	for(int i =0; i < disk_ct; i++) {
		curr_fd = curr_disk->dfile;

		// Writing sb
		if(write(curr_fd, my_sb, sizeof(struct wfs_sb)) == -1) {
			printf("Error writing to disk image\n");
			exit(-1);
		} 

		// Go to offset of inode bitmap
		lseek(curr_fd, my_sb->i_bitmap_ptr, SEEK_SET);

		// Writing inode bitmap
		if(write(curr_fd, inode_bitmap, inode_count/8) == -1) {
			printf("Error writing to disk image\n");
			exit(-1);
		} 

		// Go to offset of data bitmap
		lseek(curr_fd, my_sb->d_bitmap_ptr, SEEK_SET);

		// Writing data bitmap
		if(write(curr_fd, data_bitmap, block_count/8) == -1) {
			printf("Error writing to disk image\n");
			exit(-1);	
		}

		// Write each inode to mem
		for(int j = 0; j < inode_count;j++) {
			// Go to next inode offset
			lseek(curr_fd, my_sb->i_blocks_ptr + (j * BLOCK_SIZE), SEEK_SET);
			curr_inode.num = j; // Set inode num
			// Updating root inode
			if(curr_inode.num == 0) {
				curr_inode.nlinks = 1;
				curr_inode.size = 0;
				curr_inode.mode = S_IFDIR;
				curr_inode.uid = getuid();
				curr_inode.gid = getgid();
				curr_time = time(&curr_time);
				curr_inode.atim = curr_time;
				curr_inode.mtim = curr_time;
				curr_inode.ctim = curr_time;
				
			}
			else {
			curr_inode.nlinks = 0; // Set default 0 links	
			}

			// Writing inode
			if(write(curr_fd, &curr_inode, sizeof(struct wfs_inode)) == -1) {
				printf("Error failed to write to disk image\n");
				exit(-1);
			}
		}

		// Write blocks with garbage data?
		lseek(curr_fd, my_sb->d_blocks_ptr, SEEK_SET);
		for(int k = 0;k < block_count * BLOCK_SIZE;k++) {
			if(write(curr_fd, "0", 1) == -1) {
				printf("Error failed to write to disk image\n");
				exit(-1);
			}
		}
		close(curr_fd);	
		curr_disk = curr_disk->next;
	}
}


int main(int argc, char* argv[]) {
	raid_mode = -1;
	disk_ct = 0;
	inode_count = -1;
	block_count = -1;

	// Allocate disk linked list
	disk_ct = 0;
	disk_head = malloc(sizeof(struct Disk));
	if(disk_head == NULL) {
		printf("Error could not allocate space for disks\n");
		exit(-1);
	}

	
	parseArgs(argc, argv);


	struct wfs_sb* my_sb = malloc(sizeof(struct wfs_sb));
	if(my_sb == NULL) {
		printf("Error, couldn't allocate header\n");
		exit(-1);
	}

	int write_offset;
	
	my_sb->num_inodes = inode_count;
	my_sb->num_data_blocks = block_count;
	my_sb->i_bitmap_ptr = sizeof(struct wfs_sb); // IBITMAP comes right after SB

	write_offset = sizeof(struct wfs_sb) + (inode_count/8) + (block_count/8);
	write_offset = write_offset + (512 - (write_offset%512)); // Allign properly
	my_sb->d_bitmap_ptr = my_sb->i_bitmap_ptr + (inode_count/8); // DBITMAP comes after SB + IBITMAP
	my_sb->i_blocks_ptr = write_offset;
	my_sb->d_blocks_ptr = my_sb->i_blocks_ptr + (BLOCK_SIZE * my_sb->num_inodes);
	writeToDisk(my_sb);
	return 0;
}
