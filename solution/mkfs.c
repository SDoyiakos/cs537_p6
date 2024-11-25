#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int raid_mode; // The type of RAID being used
int disk_image_count; // The number of disk images
int inode_count; // The number of inodes
int block_count; // The number of data blocks

int parseArgs(int argc, char* argv[]) {
	char* curr_arg;
	
	// Iterate over all args
	for(int i = 1;i < argc;i++) {
		curr_arg = argv[i];
		if(strcmp(curr_arg, "-r")) {
			// Code to handle raid flag
		}
		else if(strcmp(curr_arg, "-d")) {
			// Code to handle disk flag(s)
		}
		else if(strcmp(curr_arg, "-i")) {
			// Code to handle inode count flag
		}
		else if(strcmp(curr_arg, "-b")) {
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
