mkbootimg_src_files := \
	mkbootimg.c        \
	sha.c

mkbootimg_src_obj := $(mkbootimg_src_files:.c=.o)

unpackbootimg_src_files := \
	unpackbootimg.c

unpackbootimg_src_obj := $(unpackbootimg_src_files:.c=.o)

all: mkbootimg unpackbootimg

%.o: %.c
	gcc -I. -c $<

mkbootimg: $(mkbootimg_src_obj)
	gcc $^ -o $@


unpackbootimg: $(unpackbootimg_src_obj)
	gcc $^ -o $@

.PHONY: clean
clean:
	rm -rf *.o mkbootimg unpackbootimg
