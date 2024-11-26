#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "wfs.h"

int raid_mode; // The type of RAID being used
int inode_count; // The number of inodes
int block_count; // The number of data blocks


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
				exit(1);
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
				exit(1);
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
	curr_fd = curr_disk->dfile;
	write(curr_fd, my_sb, sizeof(struct wfs_sb)); // Write sb to file

	curr_disk = curr_disk->next;
	close(curr_fd);	
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
		exit(1);
	}

	
	parseArgs(argc, argv);
	//uint64_t i_bitmap =0x0;
	//uint64_t d_bitmap =0x0;


	struct wfs_sb* my_sb = malloc(sizeof(struct wfs_sb));
	if(my_sb == NULL) {
		printf("Error, couldn't allocate header\n");
		exit(1);
	}
	my_sb->num_inodes = inode_count;
	my_sb->num_data_blocks = block_count;
	my_sb->i_bitmap_ptr = sizeof(struct wfs_sb); // IBITMAP comes right after SB
	my_sb->d_bitmap_ptr = my_sb->i_bitmap_ptr + sizeof(uint64_t); // DBITMAP comes after SB + IBITMAP
	my_sb->i_blocks_ptr = my_sb->d_bitmap_ptr + sizeof(uint64_t); // INODES come after DBITMAP + IBITMAP
	my_sb->d_blocks_ptr = my_sb->i_blocks_ptr + (inode_count * sizeof(struct wfs_inode)); // DATA comes after inodes

	writeToDisk(my_sb);
	
	return 0;
}
