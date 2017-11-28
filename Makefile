CC = gcc

mkbootimg_src_files := \
	mkbootimg.c        \
	sha.c

mkbootimg_src_obj := $(mkbootimg_src_files:.c=.o)

mkbootfs_src_files := \
        mkbootfs.c

mkbootfs_src_obj := $(mkbootfs_src_files:.c=.o)


unpackbootimg_src_files := \
	unpackbootimg.c

unpackbootimg_src_obj := $(unpackbootimg_src_files:.c=.o)

all: mkbootimg mkbootfs unpackbootimg

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(LDFLAGS) -I. -c $<

mkbootimg: $(mkbootimg_src_obj)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@

unpackbootimg: $(unpackbootimg_src_obj)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@

mkbootfs: $(mkbootfs_src_obj)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	rm -rf *.o mkbootimg unpackbootimg mkbootfs
