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

#ifndef ZC_ZODCACHE_H
#define ZC_ZODCACHE_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define ZC_SB_MAGIC		0x20DCAC8E8EACDC20l
#define ZC_UUID_BUF_SIZE	(sizeof "00000000-0000-0000-0000-000000000000")

/* These are used as array indices, so keep 'em zero-based and contiguous */
#define ZC_SB_TYPE_ORIGIN	0
#define ZC_SB_TYPE_CACHE	1
#define ZC_SB_TYPE_METADATA	2
#define ZC_SB_TYPE_COMBINED	3

/* These are used as array indices, so keep 'em zero-based and contiguous */
#define ZC_SB_MODE_WRITEBACK	0
#define ZC_SB_MODE_WRITETHROUGH	1
#define ZC_SB_MODE_PASSTHROUGH	2

/*
 * zodcache superblock
 *
 * On-disk format is always little-endian (in 64-bit chunks).  Endian
 * conversion (if required) is done a read/write time, so in-memory format is
 * host byte order.
 */
struct zc_sb_v0 {
	uint64_t	magic;
	uint64_t	cksum;
	uint64_t	version;
	uint64_t	size;
	uint64_t	type;
	uint64_t	dev_major;
	uint64_t	uuid_lo;
	uint64_t	uuid_hi;
	uint64_t	block_size;
	uint64_t	cache_mode;
	uint64_t	o_offset;
	uint64_t	o_size;
	uint64_t	c_offset;
	uint64_t	c_size;
	uint64_t	md_offset;
	uint64_t	md_size;
};

/* Ensure that the superblock struct doesn't have any padding. */
_Static_assert(sizeof(struct zc_sb_v0) ==
			offsetof(struct zc_sb_v0, md_size) + sizeof(uint64_t),
	       "Unexpected padding in struct zc_sb_v0");

/* Callback type for zc_block_size_check() and zc_sb_v0_check() */
typedef _Bool (*issue_cb_t)(char *issue, void *context);

void zc_err_set_fn(void (*err_fn)(int priority, const char *format, va_list ap));
uint64_t zc_sb_v0_cksum(const struct zc_sb_v0 *sb);
int zc_sb_v0_write(int fd, struct zc_sb_v0 *sb);
int zc_sb_v0_read(int fd, struct zc_sb_v0 *sb);
void zc_sb_v0_uuid_get(uint8_t uuid[16], const struct zc_sb_v0 *sb);
void zc_sb_v0_uuid_set(const uint8_t uuid[16], struct zc_sb_v0 *sb);
_Bool zc_block_size_check(uint64_t block_size, issue_cb_t issue_cb,
			  void *context);
_Bool zc_block_size_is_valid(uint64_t block_size);
_Bool zc_sb_v0_check(const struct zc_sb_v0 *sb, issue_cb_t issue_cb,
		     void *context);
_Bool zc_sb_v0_is_valid(const struct zc_sb_v0 *sb);
char *zc_size_format(uint64_t size, _Bool verbose);
int zc_block_size_parse(const char *s, uint64_t *block_size);
int zc_size_parse(const char *s, uint64_t *size);
const char *zc_cache_mode_format(uint64_t cache_mode, _Bool quiet);
int zc_cache_mode_parse(const char *s, uint64_t *cache_mode);
const char *zc_uuid_format(const uint8_t uuid[16], char buf[ZC_UUID_BUF_SIZE]);
const char *zc_sb_uuid_format(const struct zc_sb_v0 *sb,
			      char buf[ZC_UUID_BUF_SIZE]);
const char *zc_dev_type_format(uint64_t dev_type, _Bool quiet);
char *zc_asprintf(const char *format, ...)
				__attribute__((format(printf, 1, 2)));
void zc_err(int priority, const char *format, ...)
				__attribute__((format(printf, 2, 3)));

#endif	/* ZC_ZODCACHE_H */
