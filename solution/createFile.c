#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern errno;
int main(int argc, char* argv[]) {
	int fd;
	if(argc != 2) {
		printf("Error, you need to have one arg\n");
		return 1;
	}

	fd = open ("output.txt" ,O_CREAT|O_RDWR, 0x777);
	char entry = 'a';
	printf("Writing %d bytes\n", atoi(argv[1]));
	for(int i =0; i < atoi(argv[1]);i++) {
		write(fd, &entry, 1);
	}
	printf("Wrote to file\n");
	
	close(fd);
	return 0;
}
