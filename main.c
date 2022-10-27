#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "android_image.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define ERROR(fmt, ...) fprintf(stderr, fmt ": %s\n", ##__VA_ARGS__, strerror(errno)), EXIT_FAILURE
#define min(x, y) (x < y ? x : y)

int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("Usage: %s <kernel_file> <primary_kernel_address> <boot.img> <output>\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	char *ptr;
	long kernel_address = strtol(argv[2], &ptr, 0);

	if (!*argv[2] || *ptr || kernel_address < 0 || kernel_address > UINT32_MAX)
		return fprintf(stderr, "%s is not a valid 32 bit unsigned number\n", argv[2]), EXIT_FAILURE;

	int kernel_file = open(argv[1], O_RDONLY | O_BINARY);
	struct stat ks;
	
	if (kernel_file < 0)
		return ERROR("open(%s)", argv[1]);
	if (stat(argv[1], &ks) < 0)
		return ERROR("stat(%s)", argv[1]);
	if (ks.st_size > UINT32_MAX)
		return fprintf(stderr, "Kernel file is too big (%u > %u).\n", ks.st_size, UINT32_MAX), EXIT_FAILURE;

	int bootimg = open(argv[3], O_RDONLY | O_BINARY);
	struct stat bs;

	if (bootimg < 0)
		return ERROR("open(%s)", argv[3]);
	if (stat(argv[3], &bs) < 0)
		return ERROR("stat(%s)", argv[3]);

	int output = open(argv[4], O_WRONLY | O_BINARY | O_CREAT, 0755);

	if (output < 0)
		return ERROR("open(%s)", argv[4]);

	struct andr_img_hdr header;

	read(bootimg, &header, sizeof(header));
	if (memcmp(ANDR_BOOT_MAGIC, header.magic, sizeof(header.magic)) != 0)
		return fprintf(stderr, "Invalid bootimage header magic.\n"), EXIT_FAILURE;
	header.secondary_kernel_addr = header.kernel_addr;
	header.kernel_addr = kernel_address;
	header.secondary_kernel_size = ks.st_size;
	header.header_size += 8;
	
	char buffer[4096];
	int bytes;

	write(output, &header, sizeof(header));
	while (bytes = read(bootimg, buffer, sizeof(buffer)))
		write(output, buffer, bytes);
	close(bootimg);
	if (bs.st_size % header.page_size) {
		size_t total = header.page_size - bs.st_size % header.page_size;

		printf("Aligning boot.img (adding %u extra bytes)\n", total);
		memset(buffer, 0, sizeof(buffer));
		for (size_t i = 0; i < total; i += sizeof(buffer))
			write(output, buffer, min(sizeof(buffer), total - i));
	}
	while (bytes = read(kernel_file, buffer, sizeof(buffer)))
		write(output, buffer, bytes);
	close(kernel_file);
	if (ks.st_size % header.page_size) {
		size_t total = header.page_size - ks.st_size % header.page_size;

		printf("Aligning kernel image (adding %u extra bytes)\n", total);
		memset(buffer, 0, sizeof(buffer));
		for (size_t i = 0; i < total; i += sizeof(buffer))
			write(output, buffer, min(sizeof(buffer), total - i));
	}
	close(output);
	return EXIT_SUCCESS;
}
