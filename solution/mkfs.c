#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "wfs.h"

int raid_mode; // The type of RAID being used
int inode_count; // The number of inodes
int block_count; // The number of data blocks

// Bitmaps
unsigned char* inode_bitmap;
unsigned char* data_bitmap;


// Structs and vars for disk linked list
struct Disk {
	int dfile;
	struct Disk* next;
};
struct Disk* disk_head;
int disk_ct;

void initializeSB(struct wfs_sb* my_sb) {
  int write_offset; // The offset in where we start writing inodes									   
                                                                                                                                           
  /** Updating the super-block **/													   
  memset(my_sb, 0, sizeof(struct wfs_sb));												   
  my_sb->num_inodes = inode_count;													   
  my_sb->num_data_blocks = block_count;													   
  my_sb->i_bitmap_ptr = sizeof(struct wfs_sb); // IBITMAP comes right after SB								   
  my_sb->d_bitmap_ptr = my_sb->i_bitmap_ptr + (inode_count / 8); // DBITMAP comes after SB + IBITMAP					   
  write_offset = sizeof(struct wfs_sb) + (inode_count/8) + (block_count/8);								   
  write_offset = write_offset + (BLOCK_SIZE - (write_offset % BLOCK_SIZE)); // Allign properly						   
  my_sb->i_blocks_ptr = write_offset;													   
  my_sb->d_blocks_ptr = my_sb->i_blocks_ptr + (BLOCK_SIZE * my_sb->num_inodes); // Start data blocks at next 512 alligned region after inodes
}

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

void freeLinkedList() {
	struct Disk* prev_disk;
	struct Disk* curr_disk = disk_head;
	for(int i = 0; i < disk_ct;i++) {
		prev_disk = curr_disk;
		curr_disk = curr_disk->next;
		close(prev_disk->dfile); // Close file 
		free(prev_disk); // Free file memory
	}
	disk_ct = 0;
}

int parseArgs(int argc, char* argv[]) {
	char* curr_arg;
	
	// Iterate over all args
	for(int i = 1;i < argc - 1;i+=2) {
		curr_arg = argv[i];

		// RAID flag
		if(strcmp(curr_arg, "-r") == 0) {
			raid_mode = parseRaid(argv[i+1]);
		}

		// Disk flag
		else if(strcmp(curr_arg, "-d") == 0) {

			// Allocate new disk and error check
			struct Disk* new_disk;
			new_disk = malloc(sizeof(struct Disk));
			if(new_disk == NULL) {
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

		// Inode count flag
		else if(strcmp(curr_arg, "-i") == 0) {
			inode_count = atoi(argv[i+1]);

			// Update inode count to multiple of 32
			if(inode_count % 32 != 0) {
				inode_count += 32 - (inode_count % 32); 
			}
		}

		// Data block count flag
		else if(strcmp(curr_arg, "-b") == 0) {
			block_count = atoi(argv[i+1]);

			// Update data block count to multiple of 32
			if(block_count % 32 != 0) {
				block_count += 32 - (block_count % 32); 
			}
		}

		// Invalid input
		else {
			printf("Error, flags are not formatted properly\n");
			exit(1);
		}
	}

	// Checking all flags are set
	if( (raid_mode == -1) | (disk_ct == 0) | (inode_count == -1) | (block_count == -1) ) {
		printf("Error, not enough flags set\n");
		exit(1);
	}	
	return 0;
}

void writeToDisk(struct wfs_sb* my_sb){
	struct Disk* curr_disk = disk_head; // The disk node in the linked list
	int curr_fd; // The file descriptor of the current file being worked on
	struct wfs_inode curr_inode; // The current inode being worked on
	time_t curr_time; // The current time
	struct stat* buf; // A buffer to hold data from fstat
	off_t file_size; // Holds the file size in bytes
	char write_error[] = "Error, couldn't write to disk image\n";
	char seek_error[] = "Error seeking in disk image\n";
		

	// Allocating INode bitmap
	inode_bitmap = calloc(inode_count/8, 1); // Zero out bitmap
	if(inode_bitmap == NULL) {
		printf("Error, couldn't allocate inode bitmap\n");
		exit(-1);
	}
	*(inode_bitmap) = 1; // Setting root in bitmap

	// Allocating Data bitmap
	data_bitmap = calloc(block_count/8, 1); // Zero out bitmap
	if(data_bitmap == NULL) {
		printf("Error, couldn't allocate data bitmap\n");
		exit(-1);
	}
	
	// Do write for each disk
	for(int i = 0; i < disk_ct; i++) {
		memset(&curr_inode, 0, sizeof(struct wfs_inode)); // Zero out current inode
		curr_fd = curr_disk->dfile;

		// Retrieving file size
		buf = malloc(sizeof(struct stat));
		if(buf == NULL) {
			printf("Error allocating fstat buffer\n");
			exit(-1);
		}
		memset(buf, 0, sizeof(struct stat)); // Zero out buffer
		fstat(curr_fd, buf); // Fill the buf buffer 
		file_size = buf->st_size; // File size is the size in the buffer of the open file
		

		// Check if data needed is larger than file
		if((long unsigned int)file_size < my_sb->d_blocks_ptr + (BLOCK_SIZE * my_sb->num_data_blocks)) {
			printf("Error, disk image is too small\n");
			exit(-1);
		} 
		
		// Writing super-block
		if(write(curr_fd, my_sb, sizeof(struct wfs_sb)) == -1) {
			printf("%s", write_error);
			exit(-1);
		} 

		// Go to offset of inode bitmap
		if(lseek(curr_fd, my_sb->i_bitmap_ptr, SEEK_SET) == -1) {
			printf("%s", seek_error);
			exit(-1);
		}

		// Writing inode bitmap
		if(write(curr_fd, inode_bitmap, inode_count/8) == -1) {
			printf("%s", write_error);
			exit(-1);
		} 

		// Go to offset of data bitmap
		if(lseek(curr_fd, my_sb->d_bitmap_ptr, SEEK_SET) == -1) {
			printf("%s\n", seek_error);
			exit(-1);
		}

		// Writing data bitmap
		if(write(curr_fd, data_bitmap, block_count/8) == -1) {
			printf("%s", write_error);
			exit(-1);	
		}

		// Go to next inode offset
		if(lseek(curr_fd, my_sb->i_blocks_ptr, SEEK_SET) == -1) {
			exit(-1);
		}
		curr_inode.num = 0; // Set inode num
		
		curr_inode.nlinks = 1;
		curr_inode.size = 0;
		curr_inode.mode = S_IRWXU|S_IFDIR;
		curr_inode.uid = getuid();
		curr_inode.gid = getgid();
		curr_time = time(&curr_time);
		curr_inode.atim = curr_time;
		curr_inode.mtim = curr_time;
		curr_inode.ctim = curr_time;

		// Writing inode
		if(write(curr_fd, &curr_inode, sizeof(struct wfs_inode)) == -1) {
			printf("%s", write_error);
			exit(-1);
		}

		free(buf); // Free the buffer for file stats
		curr_disk = curr_disk->next;
	}

	// Free bitmaps
	free(data_bitmap); 
	free(inode_bitmap);
}


int main(int argc, char* argv[]) {
	raid_mode = -1;
	disk_ct = 0;
	inode_count = -1;
	block_count = -1;

	// Allocate disk linked list
	disk_ct = 0;
	
	parseArgs(argc, argv);


	struct wfs_sb* my_sb = malloc(sizeof(struct wfs_sb));
	if(my_sb == NULL) {
		printf("Error, couldn't allocate header\n");
		exit(-1);
	}

	initializeSB(my_sb); // Initialize the super-block with proper data

	writeToDisk(my_sb);

	free(my_sb);
	freeLinkedList();
	return 0;
}
