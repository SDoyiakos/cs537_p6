#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int raid_mode; // The type of RAID being used
int disk_image_count; // The number of disk images
int inode_count; // The number of inodes
int block_count; // The number of data blocks

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
			printf("The raid mode is %d\n", raid_mode);
		}
		else if(strcmp(curr_arg, "-d") == 0) {
			// Code to handle disk flag(s)
		}
		else if(strcmp(curr_arg, "-i") == 0) {
			// Code to handle inode count flag
		}
		else if(strcmp(curr_arg, "-b") == 0) {
			// Code to handle data block count flag	
		}
		else {
			printf("Error, flags are not formatted properly\n");
			exit(1);
		}
	}

	// Checking all flags are set
	if( (raid_mode == -1) | (disk_image_count == 0) | (inode_count == -1) | (block_count == -1) ) {
		printf("Error, not enough flags set\n");
		exit(1);
	}
	
	return 0;
}


int main(int argc, char* argv[]) {
	raid_mode = -1;
	disk_image_count = 0;
	inode_count = -1;
	block_count = -1;
	parseArgs(argc, argv);

	return 0;
}
