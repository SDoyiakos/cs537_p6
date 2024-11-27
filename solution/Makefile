BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`


.PHONY: all
all: $(BINS)

fuckaround:
	$(CC) $(CFLAGS) fuckaround.c $(FUSE_CFLAGS) -o fuckaround
wfs:
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs:
	$(CC) $(CFLAGS) -o mkfs mkfs.c
mkfs_sanitize:
	gcc -Og -ggdb -fsanitize=address -Wall -Werror -pedantic -std=gnu18 -g -o mkfs mkfs.c
.PHONY: clean
clean:
	rm -rf $(BINS)
