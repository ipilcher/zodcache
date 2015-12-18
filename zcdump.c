/*
 * Copyright 2015 Ian Pilcher <arequipeno@gmail.com>
 *
 * This program is free software.  You can redistribute it or modify it under
 * the terms of version 2 of the GNU General Public License (GPL), as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY -- without even the implied warranties of MERCHANTIBILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the test of the GPL for more details.
 *
 * Version 2 of the GNU General Public License is available at:
 *
 *   http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <stdio.h>
#include <fcntl.h>

#include "zodcache.h"

static _Bool sb_check_cb(char *issue, void *context __attribute__((unused)))
{
	printf("\t%s\n", issue);
	free(issue);
	return 1;
}

static void print_size(const char *const format, uint64_t size)
{
	char *const s = zc_size_format(size, /* verbose = */ 1);
	printf(format, s);
	free(s);
}

int main(int argc, char *argv[])
{
	const char *dev_type, *cache_mode;
	char uuid[ZC_UUID_BUF_SIZE];
	struct zc_sb_v0 sb;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s DEVICE\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	setlocale(LC_NUMERIC, "");

	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror(argv[1]);
		exit(EXIT_FAILURE);
	}

	if (zc_sb_v0_read(fd, &sb) < 0)
		exit(EXIT_FAILURE);

	if (close(fd) < 0) {
		perror(argv[1]);
		exit(EXIT_FAILURE);
	}

	dev_type = zc_dev_type_format(sb.type, /* quiet = */ 1);
	cache_mode = zc_cache_mode_format(sb.cache_mode, /* quiet = */ 1);

	printf("magic:\t\t%08" PRIX64 "\n", sb.magic);
	printf("checksum:\t%" PRIu64 "\n", sb.cksum);
	printf("version:\t%" PRIu64 "\n", sb.version);
	printf("size:\t\t%" PRIu64 "\n", sb.size);

	if (dev_type != NULL)
		printf("type:\t\t%s\n", dev_type);
	else
		printf("type:\t\tinvalid (%" PRIu64 ")\n", sb.type);

	printf("dev_major:\t%" PRIu64 "\n", sb.dev_major);
	printf("uuid:\t\t%s\n", zc_sb_uuid_format(&sb, uuid));
	print_size("block_size:\t%s\n", sb.block_size);

	if (cache_mode != NULL)
		printf("cache_mode:\t%s\n", cache_mode);
	else
		printf("cache_mode:\tinvalid (%" PRIu64 ")\n", sb.cache_mode);

	print_size("o_offset:\t%s\n", sb.o_offset);
	print_size("o_size:\t\t%s\n", sb.o_size);
	print_size("c_offset:\t%s\n", sb.c_offset);
	print_size("c_size:\t\t%s\n", sb.c_size);
	print_size("md_offset:\t%s\n", sb.md_offset);
	print_size("md_size:\t%s\n", sb.md_size);

	if (!zc_sb_v0_is_valid(&sb)) {
		puts("\nProblems:");
		zc_sb_v0_check(&sb, sb_check_cb, NULL);
	}

	return 0;
}
