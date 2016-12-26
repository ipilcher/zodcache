/*
 * Copyright 2015, 2016 Ian Pilcher <arequipeno@gmail.com>
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

#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>

#include <uuid/uuid.h>

#include "zodcache.h"

static uint64_t combined_cache_size(uint64_t available, uint64_t block_size)
{
	uint64_t cache_size;

	assert(block_size % 32768 == 0);	/* must be a multiple of 32KB */
	assert(block_size >= 32768);		/* min block size 32KB */
	assert(block_size <= 1073741824);	/* max block size 1GB */

	/* cache_size = (available - 4MB) * block_size / (block_size + 16) */

	/* min size (for 1 block) is 4MiB + 16 + block_size */
	if (available < 4194304 + 16 + block_size) {
		fprintf(stderr, "%d: available size too small\n", __LINE__);
		exit(EXIT_FAILURE);
	}

	cache_size = available - 4194304;

	if (cache_size >= UINT64_MAX / block_size) {
		fprintf(stderr, "%d available size too large for block size\n",
			__LINE__);
		exit(EXIT_FAILURE);
	}

	cache_size *= block_size;
	cache_size /= block_size + 16;

	return (cache_size / block_size) * block_size;
}

struct component_dev {
	const char	*path;
	uint64_t	size;
	uint64_t	major;
	int		fd;
};

static uint64_t block_size = 256 * 1024;
static uint64_t cache_mode = ZC_SB_MODE_WRITEBACK;
static uint64_t alignment = 4 * 1024;

static struct component_dev origin_dev = { .path = NULL };
static struct component_dev cache_dev = { .path = NULL };
static struct component_dev metadata_dev = { .path = NULL };

static struct zc_sb_v0 origin_sb;
static struct zc_sb_v0 cache_sb;
static struct zc_sb_v0 metadata_sb;

static int parse_dev(int argc, char *argv[], int i, const char *type,
		     struct component_dev *dev)
{
	struct stat st;

	++i;

	if (i >= argc) {
		fprintf(stderr, "%s device (%s) value missing\n",
			type, argv[i - 1]);
		exit(EXIT_FAILURE);
	}

	dev->path = argv[i];

	dev->fd = open(argv[i], O_RDWR | O_CLOEXEC | O_EXCL);
	if (dev->fd < 0)
		goto io_error;

	if (fstat(dev->fd, &st) < 0)
		goto io_error;

	if (!S_ISBLK(st.st_mode)) {
		fprintf(stderr, "%s: not a block device\n", dev->path);
		exit(EXIT_FAILURE);
	}

	if (ioctl(dev->fd, BLKGETSIZE64, &dev->size) < 0)
		goto io_error;

	dev->major = major(st.st_rdev);

	return i;

io_error:
	fprintf(stderr, "%s: %m\n", argv[i]);
	exit(EXIT_FAILURE);
}

static int parse_origin_dev(int argc, char *argv[], int i)
{
	return parse_dev(argc, argv, i, "Origin", &origin_dev);
}

static int parse_cache_dev(int argc, char *argv[], int i)
{
	return parse_dev(argc, argv, i, "Cache", &cache_dev);
}

static int parse_metadata_dev(int argc, char *argv[], int i)
{
	return parse_dev(argc, argv, i, "Metadata", &metadata_dev);
}

static int parse_block_size(int argc, char *argv[], int i)
{
	++i;

	if (i >= argc) {
		fprintf(stderr, "Block size (%s) value missing\n", argv[i - 1]);
		exit(EXIT_FAILURE);
	}

	if (zc_block_size_parse(argv[i], &block_size) < 0)
		exit(EXIT_FAILURE);

	return i;
}

static int parse_alignment(int argc, char *argv[], int i)
{
	++i;

	if (i >= argc) {
		fprintf(stderr, "Alignment (%s) value missing\n", argv[i - 1]);
		exit(EXIT_FAILURE);
	}

	if (zc_size_parse(argv[i], &alignment) < 0)
		exit(EXIT_FAILURE);

	if (alignment < 4096) {
		fprintf(stderr, "Alignment (%s) too small\n", argv[i]);
		exit(EXIT_FAILURE);
	}

	if ((alignment & (alignment - 1)) != 0) {
		fprintf(stderr, "Alignment (%s) not a power of 2\n", argv[i]);
		exit(EXIT_FAILURE);
	}

	return i;
}

static int parse_cache_mode(int argc, char *argv[], int i)
{
	++i;

	if (i >= argc) {
		fprintf(stderr, "Cache mode (%s) value missing\n", argv[i - 1]);
		exit(EXIT_FAILURE);
	}

	if (zc_cache_mode_parse(argv[i], &cache_mode) < 0)
		exit(EXIT_FAILURE);

	return i;
}

static void parse_args(int argc, char *argv[])
{
	static const struct {
		const char	*opt;
		int		(*parse_fn)(int, char **, int);
	}
	options[] = {
		{ "-o", parse_origin_dev },
		{ "-c", parse_cache_dev },
		{ "-m", parse_metadata_dev },
		{ "-b", parse_block_size },
		{ "-M", parse_cache_mode },
		{ "-a", parse_alignment },
		{ NULL, 0 }
	};

	int i, j;

	for (i = 1; i < argc; ++i) {

		for (j = 0; options[j].opt != NULL; ++j) {

			if (strcmp(options[j].opt, argv[i]) == 0) {
				i = options[j].parse_fn(argc, argv, i);
				goto continue_outer_loop;
			}
		}

		fprintf(stderr, "Unknown option: %s\n", argv[i]);
		exit(EXIT_FAILURE);

continue_outer_loop: ;
	}

	if (origin_dev.path == NULL) {
		fputs("No origin device (-o) specified\n", stderr);
		exit(EXIT_FAILURE);
	}

	if (cache_dev.path == NULL) {
		fputs("No cache device (-c) specified\n", stderr);
		exit(EXIT_FAILURE);
	}
}

static void set_origin_sb(const uint8_t *const uuid)
{
	memset(&origin_sb, 0, sizeof origin_sb);

	origin_sb.magic = ZC_SB_MAGIC;
	origin_sb.version = 0;
	origin_sb.size = sizeof origin_sb;
	origin_sb.type = ZC_SB_TYPE_ORIGIN;
	origin_sb.dev_major = origin_dev.major;
	zc_sb_v0_uuid_set(uuid, &origin_sb);
	origin_sb.block_size = block_size;
	origin_sb.cache_mode = cache_mode;
	origin_sb.o_offset = alignment;
	origin_sb.o_size = origin_dev.size;

	origin_sb.cksum = zc_sb_v0_cksum(&origin_sb);
}

static void set_cache_sb_separate(const uuid_t uuid)
{
	memset(&cache_sb, 0, sizeof cache_sb);

	cache_sb.magic = ZC_SB_MAGIC;
	cache_sb.version = 0;
	cache_sb.size = sizeof cache_sb;
	cache_sb.type = ZC_SB_TYPE_CACHE;
	cache_sb.dev_major = cache_dev.major;
	zc_sb_v0_uuid_set(uuid, &cache_sb);
	cache_sb.block_size = block_size;
	cache_sb.cache_mode = cache_mode;
	cache_sb.c_offset = alignment;
	cache_sb.c_size = cache_dev.size;

	cache_sb.cksum = zc_sb_v0_cksum(&cache_sb);
}

static void set_metadata_sb(const uuid_t uuid)
{
	memset(&metadata_sb, 0, sizeof metadata_sb);

	metadata_sb.magic = ZC_SB_MAGIC;
	metadata_sb.version = 0;
	metadata_sb.size = sizeof metadata_sb;
	metadata_sb.type = ZC_SB_TYPE_METADATA;
	metadata_sb.dev_major = metadata_dev.major;
	zc_sb_v0_uuid_set(uuid, &metadata_sb);
	metadata_sb.block_size = block_size;
	metadata_sb.cache_mode = cache_mode;
	metadata_sb.md_offset = alignment;
	metadata_sb.md_size = metadata_dev.size;

	metadata_sb.cksum = zc_sb_v0_cksum(&metadata_sb);
}

static void set_cache_sb_combined(const uuid_t uuid)
{
	memset(&cache_sb, 0, sizeof cache_sb);

	cache_sb.magic = ZC_SB_MAGIC;
	cache_sb.version = 0;
	cache_sb.size = sizeof cache_sb;
	cache_sb.type = ZC_SB_TYPE_COMBINED;
	cache_sb.dev_major = cache_dev.major;
	zc_sb_v0_uuid_set(uuid, &cache_sb);
	cache_sb.block_size = block_size;
	cache_sb.cache_mode = cache_mode;
	cache_sb.md_offset = alignment;
	cache_sb.md_size = metadata_dev.size;
	cache_sb.c_offset = cache_sb.md_offset + cache_sb.md_size;
	cache_sb.c_size = cache_dev.size;

	cache_sb.cksum = zc_sb_v0_cksum(&cache_sb);
}

int main(int argc, char *argv[])
{
	char buf[ZC_UUID_BUF_SIZE];
	uuid_t uuid;

	parse_args(argc, argv);
	uuid_generate(uuid);

	origin_dev.size -= alignment;
	set_origin_sb(uuid);

	cache_dev.size -= alignment;

	if (metadata_dev.path == NULL) {

		metadata_dev.size = cache_dev.size;
		cache_dev.size = combined_cache_size(cache_dev.size,
						     block_size);
		metadata_dev.size -= cache_dev.size;

		set_cache_sb_combined(uuid);
	}
	else {
		uint64_t nr_blocks = cache_dev.size / block_size;
		metadata_dev.size -= alignment;
		if (metadata_dev.size <	4 * 1024 * 1024 + 16 * nr_blocks) {
			fputs("Metadata device too small\n", stderr);
			exit(EXIT_FAILURE);
		}

		set_cache_sb_separate(uuid);
		set_metadata_sb(uuid);
	}

	if (zc_sb_v0_write(origin_dev.fd, &origin_sb) < 0)
		exit(EXIT_FAILURE);

	if (zc_sb_v0_write(cache_dev.fd, &cache_sb) < 0)
		exit(EXIT_FAILURE);

	if (metadata_dev.path != NULL) {
		if (zc_sb_v0_write(metadata_dev.fd, &metadata_sb) < 0)
			exit(EXIT_FAILURE);
	}

	uuid_unparse(uuid, buf);
	puts(buf);

	return 0;
}
