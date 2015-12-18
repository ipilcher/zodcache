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

#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <libdevmapper.h>

#include "zodcache.h"

#define COMPONENT_UDEV_FLAGS	(DM_UDEV_DISABLE_LIBRARY_FALLBACK |	\
					DM_UDEV_DISABLE_OTHER_RULES_FLAG)

#define ZC_DEV_UDEV_FLAGS	DM_UDEV_DISABLE_LIBRARY_FALLBACK

static int task_run_sync(struct dm_task *const task, const uint16_t udev_flags)
{
	uint32_t cookie;
	int ret;

	cookie = 0;

	if (!dm_task_set_cookie(task, &cookie, udev_flags))
		return 0;

	ret = dm_task_run(task);

	if (!dm_udev_wait(cookie))
		return 0;

	return ret;
}

static void do_component(const char *const dev, const char *const type,
			 const uint64_t offset, const uint64_t size,
			 const char *const uuid)
{
	struct dm_task *task;
	char *name, *params;

	name = zc_asprintf("zodcache-%s-%s", uuid, type);
	params = zc_asprintf("%s %" PRIu64, dev, offset / 512);

	if (		!(task = dm_task_create(DM_DEVICE_CREATE))	||

			!dm_task_enable_checks(task)			||

			!dm_task_set_name(task, name)			||

			!dm_task_add_target(task, 0, size / 512,
					"linear", params)		||

			!dm_task_set_add_node(task,
					      DM_ADD_NODE_ON_RESUME)	||

			!task_run_sync(task, COMPONENT_UDEV_FLAGS)	) {

		exit(EXIT_FAILURE);
	}

	dm_task_destroy(task);
	free(params);
	free(name);
}

static uint64_t get_dev_size(const char *const dev)
{
	uint64_t size;
	int fd;

	fd = open(dev, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		zc_err(LOG_ERR, "%s: %m\n", dev);
		exit(EXIT_FAILURE);
	}

	if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
		zc_err(LOG_ERR, "%s: %m\n", dev);
		exit(EXIT_FAILURE);
	}

	if (close(fd) < 0) {
		zc_err(LOG_ERR, "%s: %m\n", dev);
		exit(EXIT_FAILURE);
	}

	return size;
}

static void try_assemble(const struct zc_sb_v0 *const sb,
			 const char *const uuid)
{
	char *name, *params, *o_dev, *c_dev, *md_dev;
	struct dm_task *task;
	uint64_t o_size;

	o_dev = zc_asprintf("/dev/mapper/zodcache-%s-origin", uuid);
	c_dev = zc_asprintf("/dev/mapper/zodcache-%s-cache", uuid);
	md_dev = zc_asprintf("/dev/mapper/zodcache-%s-metadata", uuid);

	if (sb->type == ZC_SB_TYPE_ORIGIN) {
		if (access(c_dev, F_OK) != 0 || access(md_dev, F_OK) != 0)
			return;
	}
	else if (sb->type == ZC_SB_TYPE_CACHE) {
		if (access(o_dev, F_OK) != 0 || access(md_dev, F_OK) != 0)
			return;
	}
	else if (sb->type == ZC_SB_TYPE_METADATA) {
		if (access(o_dev, F_OK) != 0 || access(c_dev, F_OK) != 0)
			return;
	}
	else {	/* ZC_SB_TYPE_COMBINED */
		if (access(o_dev, F_OK) != 0)
			return;
	}

	if (sb->type == ZC_SB_TYPE_ORIGIN)
		o_size = sb->o_size;
	else
		o_size = get_dev_size(o_dev);

	name = zc_asprintf("zodcache-%s", uuid);
	params = zc_asprintf("%s %s %s %" PRIu64 " 1 %s default 0",
			     md_dev, c_dev, o_dev, sb->block_size / 512,
			     zc_cache_mode_format(sb->cache_mode, 0));

	if (		!(task = dm_task_create(DM_DEVICE_CREATE))	||

			!dm_task_enable_checks(task)			||

			!dm_task_set_name(task, name)			||

			!dm_task_add_target(task, 0, o_size / 512,
					    "cache", params)		||

			!dm_task_set_add_node(task,
					      DM_ADD_NODE_ON_RESUME)	||

			!task_run_sync(task, ZC_DEV_UDEV_FLAGS)		) {

		exit(EXIT_FAILURE);
	}

	dm_task_destroy(task);
	free(params);
	free(name);
	free(md_dev);
	free(c_dev);
	free(o_dev);
}

static void usage_error(const char *const name)
{
	fprintf(stderr, "Usage: %s [--udev] DEVICE\n", name);
	exit(EXIT_FAILURE);
}

static void libdm_log_fn(const int level __attribute__((unused)),
			 const char *const file __attribute__((unused)),
			 const int line __attribute__((unused)),
			 const int dm_errno_or_class __attribute__((unused)),
			 const char *const format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsyslog(LOG_PRI(level), format, ap);
	va_end(ap);
}

static void wait_for_dev(const char *const dev)
{
	int fd;

	while (1) {

		fd = open(dev, O_RDONLY | O_EXCL | O_CLOEXEC);
		if (fd >= 0)
			break;

		if (errno != EBUSY) {
			zc_err(LOG_ERR, "%s: %m\n", dev);
			exit(EXIT_FAILURE);
		}

		usleep(100000);
	}

	if (close(fd) < 0) {
		zc_err(LOG_ERR, "%s: %m\n", dev);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	char uuid[ZC_UUID_BUF_SIZE];
	struct zc_sb_v0 sb;
	struct stat st;
	_Bool udev;
	int fd;

	if (argc == 3) {
		if (strcmp(argv[1], "--udev") != 0)
			usage_error(argv[0]);
		udev = 1;
		openlog("udev-zodcache", LOG_PID, LOG_USER);
		setlogmask(LOG_UPTO(LOG_INFO));
		zc_err_set_fn(vsyslog);
		dm_log_with_errno_init(libdm_log_fn);
	}
	else if (argc == 2) {
		udev = 0;
	}
	else {
		usage_error(argv[0]);
	}

	fd = open(argv[1 + udev], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		zc_err(LOG_ERR, "%s: %m\n", argv[1 + udev]);
		exit(EXIT_FAILURE);
	}

	if (fstat(fd, &st) < 0) {
		zc_err(LOG_ERR, "%s: %m\n", argv[1 + udev]);
		exit(EXIT_FAILURE);
	}

	if (!S_ISBLK(st.st_mode)) {
		zc_err(LOG_ERR, "%s: not a block device\n", argv[1 + udev]);
		exit(EXIT_FAILURE);
	}

	if (zc_sb_v0_read(fd, &sb) < 0)
		exit(EXIT_FAILURE);

	if (close(fd) < 0) {
		zc_err(LOG_ERR, "%s: %m\n", argv[1 + udev]);
		exit(EXIT_FAILURE);
	}

	if (udev && (sb.magic != ZC_SB_MAGIC))
		exit(EXIT_SUCCESS);

	if (!zc_sb_v0_is_valid(&sb)) {
		zc_err(udev ? LOG_NOTICE : LOG_ERR,
		       "%s: invalid superblock (zcdump %s for more info)\n",
		       argv[1 + udev], argv[1 + udev]);
		exit(udev ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	if (sb.dev_major != major(st.st_rdev)) {
		zc_err(udev ? LOG_NOTICE : LOG_ERR,
		       "%s: device major number mismatch "
		       "(zcdump %s for more info)\n",
		       argv[1 + udev], argv[1 + udev]);
		exit(udev ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	zc_sb_uuid_format(&sb, uuid);
	dm_udev_set_sync_support(1);
	wait_for_dev(argv[1 + udev]);

	switch (sb.type) {

		case ZC_SB_TYPE_ORIGIN:

			do_component(argv[1 + udev], "origin", sb.o_offset,
				     sb.o_size, uuid);
			break;

		case ZC_SB_TYPE_CACHE:

			do_component(argv[1 + udev], "cache", sb.c_offset,
				     sb.c_size, uuid);
			break;

		case ZC_SB_TYPE_METADATA:

			do_component(argv[1 + udev], "metadata", sb.md_offset,
				     sb.md_size, uuid);
			break;

		case ZC_SB_TYPE_COMBINED:

			do_component(argv[1 + udev], "cache", sb.c_offset,
				     sb.c_size, uuid);
			do_component(argv[1 + udev], "metadata", sb.md_offset,
				     sb.md_size, uuid);
			break;

		default:

			/* Should never get here */
			abort();
	}

	try_assemble(&sb, uuid);

	return 0;
}
