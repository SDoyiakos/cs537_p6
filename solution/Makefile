BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`


.PHONY: all
all: $(BINS)

run_wfs_fuse: wfs mnt
	./wfs disk.img disk1.img -f -s mnt

mnt: 
	mkdir mnt
run_wfs: 
	./wfs disk.img disk1.img

run_wfs_valgrind:
	valgrind ./wfs disk.img disk1.img

redo_disks: mkfs
	$(MAKE) remove_disks
	$(MAKE) make_disk
	$(MAKE) duplicate_disk
	$(MAKE) make_disk
	./mkfs -r 1 -d disk.img -d disk1.img -i 32 -b 200	

redo_disks_zero: mkfs
	$(MAKE) remove_disks
	$(MAKE) make_disk
	$(MAKE) duplicate_disk
	$(MAKE) make_disk
	./mkfs -r 0 -d disk.img -d disk1.img -i 32 -b 200	

duplicate_disk: disk.img
	mv disk.img disk1.img
make_disk:
	./create_disk.sh
remove_disks:
	rm -rf *.img

wfs: wfs.c
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs

wfs_sanitize: wfs.c
	$(CC) -Og -ggdb -fsanitize=address $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs	

wfs_valgrind: wfs.c
	$(CC) -Og -ggdb $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs	

mkfs: mkfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c

mkfs_sanitize:
	gcc -Og -ggdb -fsanitize=address -Wall -Werror -pedantic -std=gnu18 -g -o mkfs_sanitize mkfs.c

mkfs_valgrind:
	gcc -Og -ggdb -Wall -Werror -pedantic -std=gnu18 -g -o mkfs_valgrind mkfs.c
.PHONY: clean

createFile: createFile.c
	gcc $(CFLAGS) createFile.c -o createFile

clean:
	rm -rf $(BINS)
