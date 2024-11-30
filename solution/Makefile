BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`


.PHONY: all
all: $(BINS)

wfs: wfs.c
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs: mkfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c
mkfs_sanitize:
	gcc -Og -ggdb -fsanitize=address -Wall -Werror -pedantic -std=gnu18 -g -o mkfs_sanitize mkfs.c

mkfs_valgrind:
	gcc -Og -ggdb -Wall -Werror -pedantic -std=gnu18 -g -o mkfs_valgrind mkfs.c
.PHONY: clean
clean:
	rm -rf $(BINS)
