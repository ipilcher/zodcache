#define _GNU_SOURCE

#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#include <byteswap.h>
#elif __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "__BYTE_ORDER__ is not __ORDER_BIG_ENDIAN__ or __ORDER_LITTLE_ENDIAN__"
#endif

#include "zodcache.h"

/* Number of elements in uint64_t[] representation of superblock */
#define ZC_SB_V0_NELEM		(sizeof(struct zc_sb_v0) / sizeof(uint64_t))

/* Index of checksum in uint64_t[] representation of superblock */
#define ZC_SB_V0_CKSUM_IDX	\
			(offsetof(struct zc_sb_v0, cksum) / sizeof(uint64_t))

/* Bytes reserved for superblock at beginning of each component device */
#define ZC_SB_RSVD_SIZE		4096

static void zc_err_stderr(int priority __attribute__((unused)),
			  const char *const format, va_list ap)
{
	vfprintf(stderr, format, ap);
}

static void (*zc_err_fn)(int priority, const char *format, va_list ap) =
								zc_err_stderr;

void zc_err_set_fn(void (*err_fn)(int priority, const char *format, va_list ap))
{
	if (err_fn == 0)
		zc_err_fn = zc_err_stderr;
	else
		zc_err_fn = err_fn;
}

void zc_err(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	zc_err_fn(priority, format, ap);
	va_end(ap);
}

uint64_t zc_sb_v0_cksum(const struct zc_sb_v0 *const sb)
{
	uint64_t *const a = (uint64_t *)sb;
	uint64_t s1, s2, cksum;
	uint32_t block;
	unsigned i;

	for (i = 0, s1 = 0, s2 = 0; i < ZC_SB_V0_NELEM; ++i) {

		if (i != ZC_SB_V0_CKSUM_IDX) {
			block = a[i] & 0xffffffff;
			s1 += block;
			s1 %= 4294967291;	/* largest uint32_t prime */
		}

		s2 += s1;
		s2 %= 4294967291;

		if (i != ZC_SB_V0_CKSUM_IDX) {
			block = a[i] >> 32;
			s1 += block;
			s1 %= 4294967291;
		}

		s2 += s1;
		s2 %= 4294967291;
	}

	cksum = s2;
	cksum <<= 32;
	cksum |= s1;

	return cksum;
}

static void zc_sb_v0_byteswap(struct zc_sb_v0 *const sb __attribute__((unused)))
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint64_t *const a = (uint64_t *)sb;
	unsigned i;

	for (i = 0; i < ZC_SB_V0_NELEM; ++i)
		a[i] = bswap_64(a[i]);
#endif
}

int zc_sb_v0_write(const int fd, struct zc_sb_v0 *const sb)
{
	ssize_t ret;

	zc_sb_v0_byteswap(sb);

	ret = write(fd, sb, sizeof *sb);
	if (ret < 0) {
		zc_err(LOG_ERR,
		       "Failed to write component device superblock: %m\n");
		ret = -1;
	}
	else if (ret != sizeof *sb) {
		zc_err(LOG_ERR, "Failed to write component device superblock: "
		       "Incorrect write size (wrote %zd bytes; expected %zu)\n",
		       ret, sizeof *sb);
		ret = -1;
	}
	else {
		ret = 0;
	}

	zc_sb_v0_byteswap(sb);

	return ret;
}

int zc_sb_v0_read(const int fd, struct zc_sb_v0 *const sb)
{
	ssize_t ret;

	ret = read(fd, sb, sizeof *sb);
	if (ret < 0) {
		zc_err(LOG_ERR,
		       "Failed to read component device superblock: %m\n");
		return -1;
	}
	else if (ret != sizeof *sb) {
		zc_err(LOG_ERR, "Failed to read component device superblock: "
		       "Incorrect read size (read %zd bytes; expected %zu)\n",
		       ret, sizeof *sb);
		return -1;
	}

	zc_sb_v0_byteswap(sb);

	return 0;
}

void zc_sb_v0_uuid_set(const uint8_t *const uuid, struct zc_sb_v0 *const sb)
{
	uint64_t lo, hi;

	memcpy(&lo, uuid, 8);
	memcpy(&hi, uuid + 8, 8);

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	lo = bswap_64(lo);
	hi = bswap_64(hi);
#endif

	sb->uuid_lo = lo;
	sb->uuid_hi = hi;
}

void zc_sb_v0_uuid_get(uint8_t *const uuid, const struct zc_sb_v0 *const sb)
{
	uint64_t lo, hi;

	lo = sb->uuid_lo;
	hi = sb->uuid_hi;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	lo = bswap_64(lo);
	hi = bswap_64(hi);
#endif

	memcpy(uuid, &lo, 8);
	memcpy(uuid + 8, &hi, 8);
}

static void *zc_malloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (p == NULL) {
		zc_err(LOG_CRIT, "Memory allocation failure. Aborting.\n");
		abort();
	}

	return p;
}

static char *zc_strdup(const char *const s)
{
	size_t size;
	char *p;

	size = strlen(s) + 1;
	p = zc_malloc(size);
	memcpy(p, s, size);

	return p;
}

char *zc_asprintf(const char *const fmt, ...)
{
	va_list ap;
	char *s;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&s, fmt, ap);
	va_end(ap);

	if (ret == -1) {
		zc_err(LOG_CRIT,
		       "Failed to format or allocate output. Aborting.\n");
		abort();
	}

	return s;
}

_Bool zc_block_size_check(const uint64_t block_size, const issue_cb_t issue_cb,
			  void *const context)
{
	const char *i;

	if (block_size < 32768) {
		i = "Block size smaller than 32 KiB (32768 bytes)";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}


	if (block_size > 1073741824) {
		i = "Block size larger than 1 GiB (1073741824 bytes)";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (block_size % 32768 != 0) {
		i = "Block size not a multiple of 32 KiB (32768 bytes)";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	return 1;
}

_Bool zc_block_size_is_valid(const uint64_t block_size)
{
	return zc_block_size_check(block_size, 0, NULL);
}

static  _Bool zc_sb_v0_origin_dev_check(const struct zc_sb_v0 *const sb,
					const issue_cb_t issue_cb,
					void *const context)
{
	const char *i;

	if (sb->o_offset == 0) {
		i = "Origin offset not set for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->o_size == 0) {
		i = "Origin size not set for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_offset != 0) {
		i = "Non-zero cache offset for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_size != 0) {
		i = "Non-zero cache size for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_offset != 0) {
		i = "Non-zero metadata offset for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_size != 0) {
		i = "Non-zero metadata size for origin device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	return 1;
}

static _Bool zc_sb_v0_cache_dev_check(const struct zc_sb_v0 *const sb,
				      const issue_cb_t issue_cb,
				      void *const context)
{
	const char *i;

	if (sb->o_offset != 0) {
		i = "Non-zero origin offset for cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->o_size != 0) {
		i = "Non-zero origin size for cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_offset == 0) {
		i = "Cache offset not set for cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_size == 0) {
		i = "Cache size not set for cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_offset != 0) {
		i = "Non-zero metadata offset for (non-combined) cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_size != 0) {
		i = "Non-zero metadata size for (non-combined) cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	return 1;
}

static _Bool zc_sb_v0_metadata_dev_check(const struct zc_sb_v0 *const sb,
					 const issue_cb_t issue_cb,
					 void *const context)
{
	const char *i;

	if (sb->o_offset != 0) {
		i = "Non-zero origin offset for metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->o_size != 0) {
		i = "Non-zero origin size for metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_offset != 0) {
		i = "Non-zero cache offset for (non-combined) metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_size != 0) {
		i = "Non-zero cache size for (non-combined) metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_offset == 0) {
		i = "Metadata offset not set for metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_size == 0) {
		i = "Metadata size not set for metadata device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	return 1;
}

static _Bool zc_sb_v0_combined_dev_check(const struct zc_sb_v0 *const sb,
					 const issue_cb_t issue_cb,
					 void *const context)
{
	const char *i;

	if (sb->o_offset != 0) {
		i = "Non-zero origin offset for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->o_size != 0) {
		i = "Non-zero origin size for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_offset == 0) {
		i = "Cache offset not set for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->c_size == 0) {
		i = "Cache size not set for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_offset == 0) {
		i = "Metadata offset not set for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->md_size == 0) {
		i = "Metadata size not set for combined cache device";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	return 1;
}

_Bool zc_sb_v0_check(const struct zc_sb_v0 *const sb, const issue_cb_t issue_cb,
		     void *const context)
{
	const char *i;

	if (sb->magic != ZC_SB_MAGIC) {
		i = "Incorrect magic number";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->cksum != zc_sb_v0_cksum(sb)) {
		i = "Incorrect superblock checksum";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->version != 0) {
		i = "Incorrect superblock version";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->size != sizeof *sb) {
		i = "Incorrect superblock size";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (zc_block_size_check(sb->block_size, issue_cb, context) == 0)
		return 0;

	if (sb->cache_mode > ZC_SB_MODE_PASSTHROUGH) {
		i = "Invalid cache mode";
		if (issue_cb == 0 || issue_cb(zc_strdup(i), context) == 0)
			return 0;
	}

	if (sb->type == ZC_SB_TYPE_ORIGIN)
		return zc_sb_v0_origin_dev_check(sb, issue_cb, context);

	 if (sb->type == ZC_SB_TYPE_CACHE)
		return zc_sb_v0_cache_dev_check(sb, issue_cb, context);

	if (sb->type == ZC_SB_TYPE_METADATA)
		return zc_sb_v0_metadata_dev_check(sb, issue_cb, context);

	if (sb->type == ZC_SB_TYPE_COMBINED)
		return zc_sb_v0_combined_dev_check(sb, issue_cb, context);

	if (issue_cb == 0)
		return 0;

	return issue_cb(zc_strdup("Invalid device type"), context);
}

_Bool zc_sb_v0_is_valid(const struct zc_sb_v0 *const sb)
{
	return zc_sb_v0_check(sb, 0, NULL);
}

char *zc_size_format(const uint64_t size, const _Bool verbose)
{
	static const char *const fmts[][2] = {
		[0]	= { "%" PRIu64 "G",	"%'" PRIu64 " GiB" },
		[1]	= { "%" PRIu64 "M",	"%'" PRIu64 " MiB" },
		[2]	= { "%" PRIu64 "K",	"%'" PRIu64 " KiB" },
		[3]	= { "%" PRIu64,		"%'" PRIu64 " bytes" }
	};

	if (size != 0) {

		if (size % 1073741824 == 0)
			return zc_asprintf(fmts[0][verbose], size / 1073741824);

		if (size % 1048576 == 0)
			return zc_asprintf(fmts[1][verbose], size / 1048576);

		if (size % 1024 == 0)
			return zc_asprintf(fmts[2][verbose], size / 1024);
	}

	return zc_asprintf(fmts[3][verbose], size);
}

static _Bool zc_block_size_parse_cb(char *const issue, void *const context)
{
	char **const i = context;
	*i = issue;
	return 0;
}

int zc_block_size_parse(const char *const s, uint64_t *const block_size)
{
	long size, unit;
	char *p;

	errno = 0;
	size = strtol(s, &p, 0);
	if (errno != 0 || p == s)
		goto parse_error;

	if (size < 0) {
		p = zc_strdup("Negative block size");
		goto invalid_size;
	}

	switch (*p) {
		case '\0':			unit = 1;		break;
		case 'k': 	case 'K':	unit = 1024;		break;
		case 'm': 	case 'M':	unit = 1048576; 	break;
		case 'g': 	case 'G':	unit = 1073741824;	break;
		default:			goto parse_error;
	}

	if (unit != 1 && *(++p) != '\0')
		goto parse_error;

	if (size >= LONG_MAX / unit) {
		p = "Block size larger than 1 GiB (1073741824 bytes)";
		p = zc_strdup(p);
		goto invalid_size;
	}

	size *= unit;

	if (!zc_block_size_check((uint64_t)size, zc_block_size_parse_cb, &p))
		goto invalid_size;

	*block_size = (uint64_t)size;
	return 0;

invalid_size:
	zc_err(LOG_WARNING, "Invalid block size: %s: %s\n", s, p);
	free(p);
	return -1;

parse_error:
	zc_err(LOG_WARNING, "Invalid block size: %s\n", s);
	return -1;
}

static const char *const zc_cache_modes[] = {
	"writeback", "writethrough", "passthrough"
};

const char *zc_cache_mode_format(const uint64_t cache_mode, const _Bool quiet)
{
	if (cache_mode > ZC_SB_MODE_PASSTHROUGH) {

		if (!quiet)
			zc_err(LOG_WARNING, "Invalid cache mode\n");

		return NULL;
	}

	return zc_cache_modes[cache_mode];
}

int zc_cache_mode_parse(const char *const s, uint64_t *const cache_mode)
{
	unsigned i;

	for (i = 0; i < ZC_SB_MODE_PASSTHROUGH; ++i) {
		if (strcasecmp(s, zc_cache_modes[i]) == 0) {
			*cache_mode = i;
			return 0;
		}
	}

	zc_err(LOG_WARNING, "Invalid cache mode: %s\n", s);
	return -1;
}

static const char *const zc_dev_types[] = {
	"origin", "cache (non-combined)", "metadata", "combined"
};

const char *zc_dev_type_format(const uint64_t dev_type, const _Bool quiet)
{
	if (dev_type > ZC_SB_TYPE_COMBINED) {

		if (!quiet)
			zc_err(LOG_WARNING, "Invalid component device type\n");

		return NULL;
	}

	return zc_dev_types[dev_type];
}

const char *zc_uuid_format(const uint8_t *const uuid, char *const buf)
{
	unsigned i;
	char *b;

	for (b = buf, i = 0; i < 4; ++i)
		b += sprintf(b, "%02x", uuid[i]);

	b += sprintf(b, "-");

	for (i = 4; i < 6; ++i)
		b += sprintf(b, "%02x", uuid[i]);

	b += sprintf(b, "-");

	for (i = 6; i < 8; ++i)
		b += sprintf(b, "%02x", uuid[i]);

	b += sprintf(b, "-");

	for (i = 8; i < 10; ++i)
		b += sprintf(b, "%02x", uuid[i]);

	b += sprintf(b, "-");

	for (i = 10; i < 16; ++i)
		b += sprintf(b, "%02x", uuid[i]);

	return buf;
}

const char *zc_sb_uuid_format(const struct zc_sb_v0 *const sb, char *const buf)
{
	uint8_t uuid[16];

	zc_sb_v0_uuid_get(uuid, sb);
	return zc_uuid_format(uuid, buf);
}
