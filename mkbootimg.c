/* tools/mkbootimg/mkbootimg.c
**
** Copyright 2007, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "sha.h"
#include "bootimg.h"

static void *load_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);

    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

static void *mtk_load_file(const char *fn, unsigned sz)  // MTK512字节数据创建函数
// return pack('a4 L a32 a472', "\x88\x16\x88\x58", $length, $header_type, "\xFF"x472);
{
	char *data;
	char data_hex[8];
	char size_hex[4];
	// 创建 KERNEL, ROOTFS, RECOVERY 标识符
	char mtk_kernel[6] = {0x4b, 0x45, 0x52, 0x4e, 0x45, 0x4c};
	char mtk_rootfs[6] = {0x52, 0x4f, 0x4f, 0x54, 0x46, 0x53};
	char mtk_recovery[8] = {0x52, 0x45, 0x43, 0x4f, 0x56, 0x45, 0x52, 0x59};
	int i;

	data = (char*) malloc(512); // 分配内存给data
	if(data == 0) goto oops;

	// Mtk magic 0x88 0x16 0x88 0x58
	data[0] = 0x88;
	data[1] = 0x16;
	data[2] = 0x88;
	data[3] = 0x58;

	sprintf(data_hex, "%08x", sz);		// 将数据大小输出成十六进制样式并赋值数组
	sscanf(data_hex, "%x", size_hex);	// 将字符串转换成十六进制并赋值数组

	// 数据大小赋值给data
	for(i=4 ; i < 8 ; i++)
		data[i] = size_hex[i-4];


	// 根据传递进来的参数创建标识符
	if(!strcmp(fn,"kernel"))
		for(i = 8; i < 14 ; i++)
			data[i] = mtk_kernel[i-8];

	if(!strcmp(fn,"rootfs"))
		for(i = 8; i < 14 ; i++)
			data[i] = mtk_rootfs[i-8];

	if(!strcmp(fn,"recovery"))
		for(i = 8; i < 16 ; i++)
			data[i] = mtk_recovery[i-8];

	// 标识符剩余部分填充为0x00
	for(; i < 40; i++) data[i] = 0x00;

	// 标识符之后的数据填充为0XFF
	for(i = 40; i < 512; i++) data[i] = 0xFF;

	return data; // 返回512字节数据

oops:
	if(data != 0) free(data); // 数据创建失败释放内存
	return 0;
}

int usage(void)
{
    fprintf(stderr,"usage: mkbootimg\n"
            "       --kernel <filename>\n"
            "       --ramdisk <filename>\n"
            "       [ --second <2ndbootloader-filename> ]\n"
            "       [ --cmdline <kernel-commandline> ]\n"
            "       [ --board <boardname> ]\n"
            "       [ --base <address> ]\n"
            "       [ --pagesize <pagesize> ]\n"
            "       [ --dt <filename> ]\n"
            "       [ --ramdisk_offset <address> ]\n"
            "       [ --tags_offset <address> ]\n"
			"       [ --mtk <ramdisk-type> ]\n"
            "       -o|--output <filename>\n"
            );
    return 1;
}



static unsigned char padding[131072] = { 0, };

int write_padding(int fd, unsigned pagesize, unsigned itemsize)
{
    unsigned pagemask = pagesize - 1;
    ssize_t count;

    if((itemsize & pagemask) == 0) {
        return 0;
    }

    count = pagesize - (itemsize & pagemask);

    if(write(fd, padding, count) != count) {
        return -1;
    } else {
        return 0;
    }
}

int main(int argc, char **argv)
{
    boot_img_hdr hdr;

    char *kernel_fn = 0;
    void *kernel_data = 0;
    char *ramdisk_fn = 0;
    void *ramdisk_data = 0;
    char *second_fn = 0;
    void *second_data = 0;
    char *cmdline = "";
    char *bootimg = 0;
    char *board = "";
    char *dt_fn = 0;
    void *dt_data = 0;
	// MTK变量定义开始
	char *ramdisk_type = "unknow";
	void *mtk_kernel_data = 0;
	void *mtk_boot_data = 0;
	void *mtk_recovery_data = 0;
	// MTK变量定义结束
    unsigned pagesize = 2048;
    int fd;
    SHA_CTX ctx;
    const uint8_t* sha;
    unsigned base           = 0x10000000;
    unsigned kernel_offset  = 0x00008000;
    unsigned ramdisk_offset = 0x01000000;
    unsigned second_offset  = 0x00f00000;
    unsigned tags_offset    = 0x00000100;
    size_t cmdlen;

    argc--;
    argv++;

    memset(&hdr, 0, sizeof(hdr));

    while(argc > 0){
        char *arg = argv[0];
        char *val = argv[1];
        if(argc < 2) {
            return usage();
        }
        argc -= 2;
        argv += 2;
        if(!strcmp(arg, "--output") || !strcmp(arg, "-o")) {
            bootimg = val;
        } else if(!strcmp(arg, "--kernel")) {
            kernel_fn = val;
        } else if(!strcmp(arg, "--ramdisk")) {
            ramdisk_fn = val;
        } else if(!strcmp(arg, "--second")) {
            second_fn = val;
        } else if(!strcmp(arg, "--cmdline")) {
            cmdline = val;
        } else if(!strcmp(arg, "--base")) {
            base = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--kernel_offset")) {
            kernel_offset = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--ramdisk_offset")) {
            ramdisk_offset = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--second_offset")) {
            second_offset = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--tags_offset")) {
            tags_offset = strtoul(val, 0, 16);
        } else if(!strcmp(arg, "--board")) {
            board = val;
        } else if(!strcmp(arg,"--pagesize")) {
            pagesize = strtoul(val, 0, 10);
            if ((pagesize != 2048) && (pagesize != 4096)
                && (pagesize != 8192) && (pagesize != 16384)
                && (pagesize != 32768) && (pagesize != 65536)
                && (pagesize != 131072)) {
                fprintf(stderr,"error: unsupported page size %d\n", pagesize);
                return -1;
            }
        } else if(!strcmp(arg, "--dt")) {
            dt_fn = val;
		// 判断"--mtk"参数是否为boot或recovery
		} else if(!strcmp(arg, "--mtk")) {
			if(!strcmp(val,"boot") || !strcmp(val,"recovery")) ramdisk_type = val;
				else return usage();
        } else {
            return usage();
        }
    }
    hdr.page_size = pagesize;

    hdr.kernel_addr =  base + kernel_offset;
    hdr.ramdisk_addr = base + ramdisk_offset;
    hdr.second_addr =  base + second_offset;
    hdr.tags_addr =    base + tags_offset;

    if(bootimg == 0) {
        fprintf(stderr,"error: no output filename specified\n");
        return usage();
    }

    if(kernel_fn == 0) {
        fprintf(stderr,"error: no kernel image specified\n");
        return usage();
    }

    if(ramdisk_fn == 0) {
        fprintf(stderr,"error: no ramdisk image specified\n");
        return usage();
    }

    if(strlen(board) >= BOOT_NAME_SIZE) {
        fprintf(stderr,"error: board name too large\n");
        return usage();
    }

    strcpy((char *) hdr.name, board);

    memcpy(hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);

    cmdlen = strlen(cmdline);
    if(cmdlen > (BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE - 2)) {
        fprintf(stderr,"error: kernel commandline too large\n");
        return 1;
    }
    /* Even if we need to use the supplemental field, ensure we
     * are still NULL-terminated */
    strncpy((char *)hdr.cmdline, cmdline, BOOT_ARGS_SIZE - 1);
    hdr.cmdline[BOOT_ARGS_SIZE - 1] = '\0';
    if (cmdlen >= (BOOT_ARGS_SIZE - 1)) {
        cmdline += (BOOT_ARGS_SIZE - 1);
        strncpy((char *)hdr.extra_cmdline, cmdline, BOOT_EXTRA_ARGS_SIZE);
    }

    kernel_data = load_file(kernel_fn, &hdr.kernel_size);
    if(kernel_data == 0) {
        fprintf(stderr,"error: could not load kernel '%s'\n", kernel_fn);
        return 1;
    }

    if(!strcmp(ramdisk_fn,"NONE")) {
        ramdisk_data = 0;
        hdr.ramdisk_size = 0;
    } else {
        ramdisk_data = load_file(ramdisk_fn, &hdr.ramdisk_size);
        if(ramdisk_data == 0) {
            fprintf(stderr,"error: could not load ramdisk '%s'\n", ramdisk_fn);
            return 1;
        }
    }

	if(second_fn) {
        second_data = load_file(second_fn, &hdr.second_size);
        if(second_data == 0) {
            fprintf(stderr,"error: could not load secondstage '%s'\n", second_fn);
            return 1;
        }
    }

    if(dt_fn) {
        dt_data = load_file(dt_fn, &hdr.dt_size);
        if (dt_data == 0) {
            fprintf(stderr,"error: could not load device tree image '%s'\n", dt_fn);
            return 1;
        }
    }

	if(!strcmp(ramdisk_type,"recovery") || !strcmp(ramdisk_type,"boot")) {
		mtk_kernel_data   = mtk_load_file("kernel",hdr.kernel_size);	// kernel头部分数据创建
		mtk_boot_data     = mtk_load_file("rootfs",hdr.ramdisk_size);	// rootfs头部分数据创建
		mtk_recovery_data = mtk_load_file("recovery",hdr.ramdisk_size);	// recovery头部分数据创建

		hdr.kernel_size  += 512;	// kernel增加512字节 (MTK头部分大小)
		hdr.ramdisk_size += 512;	// ramdisk增加512字节 (MTK头部分大小)

		if(mtk_kernel_data == 0 || mtk_boot_data == 0 || mtk_recovery_data == 0 ) { // 创建失败则退出
			fprintf(stderr,"error: can't init mtk boot.img data\n");
			return 1;
		}
	}

    /* put a hash of the contents in the header so boot images can be
     * differentiated based on their first 2k.
     */
	SHA_init(&ctx);
   	SHA_update(&ctx, kernel_data, hdr.kernel_size);
   	SHA_update(&ctx, &hdr.kernel_size, sizeof(hdr.kernel_size));
   	SHA_update(&ctx, ramdisk_data, hdr.ramdisk_size);
   	SHA_update(&ctx, &hdr.ramdisk_size, sizeof(hdr.ramdisk_size));
    SHA_update(&ctx, second_data, hdr.second_size);
    SHA_update(&ctx, &hdr.second_size, sizeof(hdr.second_size));
    if(dt_data) {
        SHA_update(&ctx, dt_data, hdr.dt_size);
        SHA_update(&ctx, &hdr.dt_size, sizeof(hdr.dt_size));
    }
    sha = SHA_final(&ctx);
    memcpy(hdr.id, sha,
           SHA_DIGEST_SIZE > sizeof(hdr.id) ? sizeof(hdr.id) : SHA_DIGEST_SIZE);

    fd = open(bootimg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if(fd < 0) {
        fprintf(stderr,"error: could not create '%s'\n", bootimg);
        return 1;
    }

    if(write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) goto fail;
    if(write_padding(fd, pagesize, sizeof(hdr))) goto fail;
	
	if(!strcmp(ramdisk_type,"recovery") || !strcmp(ramdisk_type,"boot")) {//
		hdr.kernel_size  -= 512; // kernel还原回正常写入数据大小
		hdr.ramdisk_size -= 512; // ramdisk还原回正常写入数据大小
		
		if(write(fd, mtk_kernel_data, 512) != 512) goto fail; // 将MTK标识数据写入kernel头部分
	}

    if(write(fd, kernel_data, hdr.kernel_size) != (ssize_t) hdr.kernel_size) goto fail;
    if(!strcmp(ramdisk_type,"recovery") || !strcmp(ramdisk_type,"boot")) hdr.kernel_size += 512;	// kernel大小增加512字节 (为了扇区补全)
	if(write_padding(fd, pagesize, hdr.kernel_size)) goto fail;

	if(!strcmp(ramdisk_type,"boot"))     if(write(fd, mtk_boot_data, 512) != 512) goto fail;		// 将rootfs标识数据写入ramdisk头部分
	if(!strcmp(ramdisk_type,"recovery")) if(write(fd, mtk_recovery_data, 512) != 512) goto fail;	// 将recovery标识数据写入ramdisk头部分

    if(write(fd, ramdisk_data, hdr.ramdisk_size) != (ssize_t) hdr.ramdisk_size) goto fail;
	if(!strcmp(ramdisk_type,"recovery") || !strcmp(ramdisk_type,"boot")) hdr.ramdisk_size += 512;	// ramdisk大小增加512字节 (为了扇区补全)
    if(write_padding(fd, pagesize, hdr.ramdisk_size)) goto fail;

    if(second_data) {
        if(write(fd, second_data, hdr.second_size) != (ssize_t) hdr.second_size) goto fail;
        if(write_padding(fd, pagesize, hdr.second_size)) goto fail;
    }

    if(dt_data) {
        if(write(fd, dt_data, hdr.dt_size) != (ssize_t) hdr.dt_size) goto fail;
        if(write_padding(fd, pagesize, hdr.dt_size)) goto fail;
    }
    return 0;

fail:
    unlink(bootimg);
    close(fd);
    fprintf(stderr,"error: failed writing '%s': %s\n", bootimg,
            strerror(errno));
    return 1;
}
