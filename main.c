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

struct ImgData
{
	int kernel_file;
	int bootimg;
	int output;
	long kernel_address;
	struct stat ks;
	struct stat bs;
};

int output_bootimg(struct ImgData *data)
{
	struct andr_img_hdr header;
	char buffer[4096];
	int bytes;

	memset(buffer, 0, sizeof(buffer));

	// We read the boot.img header
	read(data->bootimg, &header, sizeof(header));

	// Check if the magic is valid
	if (memcmp(ANDR_BOOT_MAGIC, header.magic, sizeof(header.magic)) != 0)
		return fprintf(stderr, "Invalid bootimage header magic.\n"), EXIT_FAILURE;

	// Check if the page_size is valid (we devide by it later)
	if (header.page_size == 0)
		return fprintf(stderr, "Invalid bootimage header.\n"), EXIT_FAILURE;
	
	// Fill the added header fields and update the size of the header
	header.secondary_kernel_addr = header.kernel_addr;
	header.kernel_addr = data->kernel_address;
	header.secondary_kernel_size = data->ks.st_size;
	header.header_size += 8;

	if (sizeof(buffer) < header.page_size)
		return fprintf(stderr, "Assertion failed sizeof(buffer) >= header.page_size.\n"), EXIT_FAILURE; 

	// Write the updated header to the output
	write(data->output, &header, sizeof(header));
	while (bytes = read(data->bootimg, buffer, sizeof(buffer)))
		write(data->output, buffer, bytes);
	close(data->bootimg);

	// Add required padding
	if (data->bs.st_size % header.page_size) {
		size_t total = header.page_size - data->bs.st_size % header.page_size;

		memset(buffer, 0, sizeof(buffer));
		for (size_t i = 0; i < total; i += sizeof(buffer))
			write(data->output, buffer, min(sizeof(buffer), total - i));
	}

	// Write kernel
	while (bytes = read(data->kernel_file, buffer, sizeof(buffer)))
		write(data->output, buffer, bytes);
		
	// We are done!
	close(data->kernel_file);
	close(data->output);
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("Usage: %s <kernel_file> <primary_kernel_address> <boot.img> <output>\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	struct ImgData data;
	char *ptr;

	data.kernel_address = strtol(argv[2], &ptr, 0);
	if (!*argv[2] || *ptr || data.kernel_address < 0 || data.kernel_address > UINT32_MAX)
		return fprintf(stderr, "%s is not a valid 32 bit unsigned number\n", argv[2]), EXIT_FAILURE;

	data.kernel_file = open(argv[1], O_RDONLY | O_BINARY);
	if (data.kernel_file < 0)
		return ERROR("open(%s)", argv[1]);
	if (stat(argv[1], &data.ks) < 0)
		return ERROR("stat(%s)", argv[1]);
	if (data.ks.st_size > UINT32_MAX)
		return fprintf(stderr, "Kernel file is too big (%u > %u).\n", data.ks.st_size, UINT32_MAX), EXIT_FAILURE;

	data.bootimg = open(argv[3], O_RDONLY | O_BINARY);
	if (data.bootimg < 0)
		return ERROR("open(%s)", argv[3]);
	if (stat(argv[3], &data.bs) < 0)
		return ERROR("stat(%s)", argv[3]);

	data.output = open(argv[4], O_WRONLY | O_BINARY | O_CREAT | O_TRUNC, 0755);
	if (data.output < 0)
		return ERROR("open(%s)", argv[4]);

	return output_bootimg(&data);
}
