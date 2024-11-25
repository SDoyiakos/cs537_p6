#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//This C program initializes a file to an empty filesystem. I.e. to the state, where the filesystem can be mounted and other files and directories can be created under the root inode. The program receives three arguments: the raid mode, disk image file (multiple times), the number of inodes in the filesystem, and the number of data blocks in the system. The number of blocks should always be rounded up to the nearest multiple of 32 to prevent the data structures on disk from being misaligned. For example:

//./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 200
//initializes all disks (disk1 and disk2) to an empty filesystem with 32 inodes and 224 data blocks. The size of the inode and data bitmaps are determined by the number of blocks specified by mkfs. If mkfs finds that the disk image file is too small to accommodate the number of blocks, it should exit with return code -1. mkfs should write the superblock and root inode to the disk image./

int main(int argc, char *argv[])
{
	if (argc < 3){
		exit(1);
	}
	
	int raid_mode = -1;
	int numDisks = 0;
	int * disks = malloc(sizeof(int));

	for(int i = 0; i < argc; i++){

		if((char)argv[i] == '-'){
			i++;

			if(argv[i] == 'r'){
				if(raid_mode == -1){
					printf("multiple arguments for raid\n");
					exit(1);
				}		
				raid_mode = argv[i + 1];
				i++;
				continue;
			}
			
			if(argv[i] == 'd'){
				numFiles++;
				int fd = open(argv[++i], O_RDWR);
				if(fd == -1){
					printf("failed to open file %s\n", argv[i]);
					exit(1);
				}	
				if(reallocarray(disks, sizeof(int), numFiles) == NULL){
					printf("failed to reallocarray\n");
					exit(1);
				}
				disks[numFiles-1] = fd;
			}
	
		}
	}	

	umask(0);
	return fuse_main(argc, argv, &xmp_oper, NULL);
}
